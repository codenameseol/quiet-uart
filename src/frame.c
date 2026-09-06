#include "frame.h"

static int emit_escaped(uint8_t byte, uint8_t *out_buf, size_t out_buf_cap, size_t *out_pos)
{
    if (byte == FRAME_DELIMITER || byte == FRAME_ESCAPE) {
        if (*out_pos + 2 > out_buf_cap) {
            return -1;
        }
        out_buf[(*out_pos)++] = (uint8_t)FRAME_ESCAPE;
        out_buf[(*out_pos)++] = (uint8_t)(byte ^ FRAME_ESCAPE_XOR);
    } else {
        if (*out_pos + 1 > out_buf_cap) {
            return -1;
        }
        out_buf[(*out_pos)++] = byte;
    }
    return 0;
}

int encode_frame(const uint8_t *payload, size_t len,
                  uint8_t *out_buf, size_t out_buf_cap)
{
    if (out_buf == NULL) {
        return -1;
    }
    if (len > FRAME_MAX_PAYLOAD) {
        return -1;
    }
    if (len > 0 && payload == NULL) {
        return -1;
    }

    uint8_t checksum = 0;
    for (size_t i = 0; i < len; i++) {
        checksum ^= payload[i];
    }

    size_t out_pos = 0;

    if (out_pos + 1 > out_buf_cap) {
        return -1;
    }
    out_buf[out_pos++] = (uint8_t)FRAME_DELIMITER;

    if (emit_escaped((uint8_t)len, out_buf, out_buf_cap, &out_pos) != 0) {
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        if (emit_escaped(payload[i], out_buf, out_buf_cap, &out_pos) != 0) {
            return -1;
        }
    }
    if (emit_escaped(checksum, out_buf, out_buf_cap, &out_pos) != 0) {
        return -1;
    }

    if (out_pos + 1 > out_buf_cap) {
        return -1;
    }
    out_buf[out_pos++] = (uint8_t)FRAME_DELIMITER;

    return (int)out_pos;
}

void frame_parser_init(frame_parser_t *parser)
{
    parser->state = FRAME_PARSER_WAIT_START;
    parser->escape_next = 0;
    parser->raw_len = 0;
    parser->payload_len = 0;
}

/* Drop whatever body bytes have been collected so far and get ready
 * for the next frame attempt. Deliberately does NOT touch `state`:
 * once the parser has seen its first opening delimiter it stays in
 * FRAME_PARSER_IN_FRAME for good (see frame_parser_feed for why). */
static void reset_body(frame_parser_t *parser)
{
    parser->escape_next = 0;
    parser->raw_len = 0;
}

/* Called when an unescaped 0x7E is seen while collecting a frame body.
 * Validates LEN and checksum, fills parser->payload on success, and
 * always clears the body buffer so the *same* byte can immediately
 * double as the opening delimiter of whatever comes next -- exactly
 * how a shared flag byte works in HDLC-style framing. */
static frame_status_t finish_frame(frame_parser_t *parser)
{
    size_t raw_len = parser->raw_len;
    frame_status_t status;

    if (raw_len < 2) {
        /* not even a LEN + checksum -- too short to be a real frame */
        status = FRAME_STATUS_ERROR_LENGTH;
    } else {
        uint8_t len_field = parser->raw[0];
        size_t payload_len = raw_len - 2;

        if (payload_len != (size_t)len_field) {
            status = FRAME_STATUS_ERROR_LENGTH;
        } else {
            uint8_t checksum = 0;
            for (size_t i = 0; i < payload_len; i++) {
                checksum ^= parser->raw[1 + i];
            }
            uint8_t received_checksum = parser->raw[raw_len - 1];

            if (checksum != received_checksum) {
                status = FRAME_STATUS_ERROR_CHECKSUM;
            } else {
                for (size_t i = 0; i < payload_len; i++) {
                    parser->payload[i] = parser->raw[1 + i];
                }
                parser->payload_len = payload_len;
                status = FRAME_STATUS_FRAME_READY;
            }
        }
    }

    reset_body(parser);
    return status;
}

frame_status_t frame_parser_feed(frame_parser_t *parser, uint8_t byte)
{
    if (parser->state == FRAME_PARSER_WAIT_START) {
        /* Pre-sync: we have never seen an opening delimiter yet, so
         * every byte that isn't one is just noise on the line --
         * drop it and keep listening quietly. This is the ONLY place
         * that ever leaves FRAME_PARSER_WAIT_START; once a frame has
         * started, 0x7E bytes are handled below instead. */
        if (byte == (uint8_t)FRAME_DELIMITER) {
            parser->state = FRAME_PARSER_IN_FRAME;
            parser->escape_next = 0;
            parser->raw_len = 0;
        }
        return FRAME_STATUS_IN_PROGRESS;
    }

    /* FRAME_PARSER_IN_FRAME */
    if (parser->escape_next) {
        parser->escape_next = 0;
        if (parser->raw_len >= FRAME_MAX_RAW) {
            reset_body(parser);
            return FRAME_STATUS_ERROR_OVERFLOW;
        }
        parser->raw[parser->raw_len++] = (uint8_t)(byte ^ FRAME_ESCAPE_XOR);
        return FRAME_STATUS_IN_PROGRESS;
    }

    if (byte == (uint8_t)FRAME_ESCAPE) {
        parser->escape_next = 1;
        return FRAME_STATUS_IN_PROGRESS;
    }

    if (byte == (uint8_t)FRAME_DELIMITER) {
        /* 0x7E is both an end delimiter and (immediately) the start
         * of the next frame attempt -- it never sends us back to
         * FRAME_PARSER_WAIT_START. This is what lets the parser shrug
         * off a stray, unmatched 0x7E sitting in noise: it just gets
         * treated as an (empty/too-short) frame that fails to
         * validate, and the very next byte starts fresh. */
        return finish_frame(parser);
    }

    if (parser->raw_len >= FRAME_MAX_RAW) {
        reset_body(parser);
        return FRAME_STATUS_ERROR_OVERFLOW;
    }
    parser->raw[parser->raw_len++] = byte;
    return FRAME_STATUS_IN_PROGRESS;
}
