#ifndef QUIET_UART_FRAME_H
#define QUIET_UART_FRAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * quiet-uart frame format
 * ------------------------
 *
 *   0x7E [LEN] [payload x LEN] [checksum] 0x7E
 *
 *   - 0x7E   frame delimiter (start AND end)
 *   - LEN    1 byte, number of payload bytes (0..255)
 *   - payload  LEN raw bytes
 *   - checksum 1 byte, XOR of all payload bytes
 *
 * Everything between the two delimiters (LEN, payload, checksum) is
 * byte-stuffed before it goes on the wire: any occurrence of 0x7E or
 * the escape byte 0x7D is replaced with the two-byte sequence
 *
 *   0x7D, (original_byte XOR 0x20)
 *
 * so that a delimiter can never appear "by accident" inside the body
 * of a frame. This is the same shape of trick PPP/HDLC-style framing
 * uses; quiet-uart just keeps the smallest possible slice of it.
 */

#define FRAME_DELIMITER   0x7Eu
#define FRAME_ESCAPE      0x7Du
#define FRAME_ESCAPE_XOR  0x20u

#define FRAME_MAX_PAYLOAD 255u
/* unescaped body = LEN(1) + payload(<=255) + checksum(1) */
#define FRAME_MAX_RAW     (1u + FRAME_MAX_PAYLOAD + 1u)

/*
 * Encode `len` payload bytes into a complete, byte-stuffed, delimited
 * frame written to out_buf.
 *
 * Returns the number of bytes written on success, or -1 if:
 *   - len > FRAME_MAX_PAYLOAD, or
 *   - out_buf is NULL, or
 *   - out_buf_cap is too small to hold the encoded frame.
 */
int encode_frame(const uint8_t *payload, size_t len,
                  uint8_t *out_buf, size_t out_buf_cap);

/* Result of feeding a single byte to the streaming parser. */
typedef enum {
    /* byte consumed, no complete frame yet -- keep feeding */
    FRAME_STATUS_IN_PROGRESS = 0,
    /* a complete, checksum-valid frame is ready in parser->payload */
    FRAME_STATUS_FRAME_READY,
    /* a frame ended but its checksum didn't match -- discarded */
    FRAME_STATUS_ERROR_CHECKSUM,
    /* a frame ended but LEN didn't match the actual byte count -- discarded */
    FRAME_STATUS_ERROR_LENGTH,
    /* a frame body grew past FRAME_MAX_RAW without closing -- discarded */
    FRAME_STATUS_ERROR_OVERFLOW
} frame_status_t;

typedef enum {
    FRAME_PARSER_WAIT_START = 0,
    FRAME_PARSER_IN_FRAME
} frame_parser_state_t;

/*
 * Streaming, single-byte-at-a-time frame parser.
 *
 * Bytes that arrive before the first 0x7E (line noise, garbage, a
 * half-received previous frame) are silently dropped while the parser
 * waits for a start delimiter -- this is the "quiet" part: it never
 * gets stuck, it just keeps listening for the next real signal.
 *
 * After that first delimiter, every 0x7E does double duty: it closes
 * whatever body was being collected (emitting a frame if it validates,
 * an error status if it doesn't) AND immediately starts the next
 * frame attempt, HDLC-style. That is what lets the parser shrug off a
 * stray, unmatched 0x7E sitting in noise instead of getting confused
 * by it.
 */
typedef struct {
    frame_parser_state_t state;
    int escape_next;

    uint8_t raw[FRAME_MAX_RAW];
    size_t raw_len;

    /* populated only when frame_parser_feed() returns FRAME_STATUS_FRAME_READY */
    uint8_t payload[FRAME_MAX_PAYLOAD];
    size_t payload_len;
} frame_parser_t;

void frame_parser_init(frame_parser_t *parser);
frame_status_t frame_parser_feed(frame_parser_t *parser, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* QUIET_UART_FRAME_H */
