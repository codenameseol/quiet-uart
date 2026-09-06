/*
 * assert()-based tests for the quiet-uart framer.
 *
 * Run via `make test`, or directly:
 *   gcc -Wall -Wextra -std=c11 -Isrc -o build/test_frame tests/test_frame.c src/frame.c
 *   ./build/test_frame
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "frame.h"

/* Feed an entire byte buffer into a parser one byte at a time, and
 * assert that exactly one FRAME_STATUS_FRAME_READY is produced, with
 * a payload equal to (expected, expected_len). Any IN_PROGRESS status
 * along the way is fine; any *other* status is a test failure unless
 * allow_errors is set (used by the noisy-stream test, where garbage
 * bytes are allowed to trip an error before the real frame arrives). */
static void expect_single_frame(const uint8_t *stream, size_t stream_len,
                                 const uint8_t *expected, size_t expected_len,
                                 int allow_errors)
{
    frame_parser_t parser;
    frame_parser_init(&parser);

    int frames_seen = 0;

    for (size_t i = 0; i < stream_len; i++) {
        frame_status_t status = frame_parser_feed(&parser, stream[i]);

        switch (status) {
            case FRAME_STATUS_IN_PROGRESS:
                break;
            case FRAME_STATUS_FRAME_READY:
                frames_seen++;
                assert(parser.payload_len == expected_len);
                assert(memcmp(parser.payload, expected, expected_len) == 0);
                break;
            case FRAME_STATUS_ERROR_CHECKSUM:
            case FRAME_STATUS_ERROR_LENGTH:
            case FRAME_STATUS_ERROR_OVERFLOW:
                assert(allow_errors && "unexpected parser error");
                break;
        }
    }

    assert(frames_seen == 1);
}

/* Test 1: plain round trip, no bytes that need escaping. */
static void test_roundtrip_plain(void)
{
    const uint8_t payload[] = { 'H', 'i', '!' };
    uint8_t encoded[64];

    int encoded_len = encode_frame(payload, sizeof(payload), encoded, sizeof(encoded));
    assert(encoded_len > 0);

    /* delimiter, LEN, 3 payload bytes, checksum, delimiter -- none of
     * these bytes happen to need escaping, so length is exact. */
    assert((size_t)encoded_len == 1 + 1 + sizeof(payload) + 1 + 1);
    assert(encoded[0] == FRAME_DELIMITER);
    assert(encoded[(size_t)encoded_len - 1] == FRAME_DELIMITER);

    expect_single_frame(encoded, (size_t)encoded_len, payload, sizeof(payload), 0);

    printf("[ok] roundtrip_plain\n");
}

/* Test 2: payload contains bytes that must be escaped (0x7E and 0x7D
 * themselves, plus a couple of neighbours) -- verifies byte-stuffing
 * both on the way out and the way back in. */
static void test_roundtrip_escaped(void)
{
    const uint8_t payload[] = { 0x7E, 0x7D, 0x00, 0x7E, 0x01, 0x7D };
    uint8_t encoded[64];

    int encoded_len = encode_frame(payload, sizeof(payload), encoded, sizeof(encoded));
    assert(encoded_len > 0);

    /* every one of the 4 escape-worthy payload bytes costs an extra
     * byte on the wire (2 bytes instead of 1). */
    size_t plain_len = 1 + 1 + sizeof(payload) + 1 + 1;
    assert((size_t)encoded_len == plain_len + 4);

    expect_single_frame(encoded, (size_t)encoded_len, payload, sizeof(payload), 0);

    printf("[ok] roundtrip_escaped\n");
}

/* Test 3: a frame whose checksum byte has been corrupted in transit
 * must be rejected, not silently accepted. */
static void test_corrupted_checksum_rejected(void)
{
    const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t encoded[64];

    int encoded_len = encode_frame(payload, sizeof(payload), encoded, sizeof(encoded));
    assert(encoded_len > 0);

    /* checksum = 0x01 ^ 0x02 ^ 0x03 ^ 0x04 = 0x04, which needs no
     * escaping, so it sits as a single plain byte right before the
     * closing delimiter. Flip a bit in it. */
    size_t checksum_pos = (size_t)encoded_len - 2;
    assert(encoded[checksum_pos] == 0x04);
    encoded[checksum_pos] ^= 0x01;

    frame_parser_t parser;
    frame_parser_init(&parser);

    int saw_checksum_error = 0;
    int saw_frame_ready = 0;

    for (int i = 0; i < encoded_len; i++) {
        frame_status_t status = frame_parser_feed(&parser, encoded[i]);
        if (status == FRAME_STATUS_ERROR_CHECKSUM) {
            saw_checksum_error = 1;
        }
        if (status == FRAME_STATUS_FRAME_READY) {
            saw_frame_ready = 1;
        }
    }

    assert(saw_checksum_error == 1);
    assert(saw_frame_ready == 0);

    printf("[ok] corrupted_checksum_rejected\n");
}

/* Test 4: the "quiet" part. A stream with garbage bytes before AND
 * after a single valid frame -- including a stray, unmatched 0x7E
 * dropped into the leading noise -- must still yield exactly the one
 * real frame, with the parser resyncing around the junk on both
 * sides. */
static void test_recovers_from_noisy_stream(void)
{
    const uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint8_t encoded[64];

    int encoded_len = encode_frame(payload, sizeof(payload), encoded, sizeof(encoded));
    assert(encoded_len > 0);

    /* leading noise: random bytes plus a stray 0x7E that starts a
     * bogus "frame" which never properly closes before the real one
     * begins -- the parser should just abandon it and resync. */
    const uint8_t leading_noise[] = { 0x00, 0xFF, 0xAB, 0x7E, 0x01, 0x02, 0x99 };
    /* trailing noise: plain garbage, no delimiters at all. */
    const uint8_t trailing_noise[] = { 0x11, 0x22, 0x33, 0xFE, 0xCD };

    uint8_t stream[128];
    size_t pos = 0;

    memcpy(stream + pos, leading_noise, sizeof(leading_noise));
    pos += sizeof(leading_noise);

    memcpy(stream + pos, encoded, (size_t)encoded_len);
    pos += (size_t)encoded_len;

    memcpy(stream + pos, trailing_noise, sizeof(trailing_noise));
    pos += sizeof(trailing_noise);

    /* allow_errors=1: the stray 0x7E in the leading noise opens a
     * frame that later gets abandoned/overwritten, which may or may
     * not surface as an error status depending on exactly where it
     * gets cut off -- what matters is that the *real* frame still
     * comes through cleanly exactly once. */
    expect_single_frame(stream, pos, payload, sizeof(payload), 1);

    printf("[ok] recovers_from_noisy_stream\n");
}

/* Extra coverage: two valid frames back-to-back (closing delimiter of
 * frame 1 doubles as noise boundary before frame 2's opening
 * delimiter) should both be recovered, in order. */
static void test_two_frames_back_to_back(void)
{
    const uint8_t payload_a[] = { 'a', 'b', 'c' };
    const uint8_t payload_b[] = { 'x', 'y' };
    uint8_t encoded_a[64];
    uint8_t encoded_b[64];

    int len_a = encode_frame(payload_a, sizeof(payload_a), encoded_a, sizeof(encoded_a));
    int len_b = encode_frame(payload_b, sizeof(payload_b), encoded_b, sizeof(encoded_b));
    assert(len_a > 0 && len_b > 0);

    uint8_t stream[128];
    size_t pos = 0;
    memcpy(stream + pos, encoded_a, (size_t)len_a);
    pos += (size_t)len_a;
    memcpy(stream + pos, encoded_b, (size_t)len_b);
    pos += (size_t)len_b;

    frame_parser_t parser;
    frame_parser_init(&parser);

    int frame_index = 0;
    for (size_t i = 0; i < pos; i++) {
        frame_status_t status = frame_parser_feed(&parser, stream[i]);
        if (status == FRAME_STATUS_FRAME_READY) {
            if (frame_index == 0) {
                assert(parser.payload_len == sizeof(payload_a));
                assert(memcmp(parser.payload, payload_a, sizeof(payload_a)) == 0);
            } else if (frame_index == 1) {
                assert(parser.payload_len == sizeof(payload_b));
                assert(memcmp(parser.payload, payload_b, sizeof(payload_b)) == 0);
            }
            frame_index++;
        }
    }

    assert(frame_index == 2);

    printf("[ok] two_frames_back_to_back\n");
}

int main(void)
{
    test_roundtrip_plain();
    test_roundtrip_escaped();
    test_corrupted_checksum_rejected();
    test_recovers_from_noisy_stream();
    test_two_frames_back_to_back();

    printf("All tests passed.\n");
    return 0;
}
