# quiet-uart

<details open><summary>언어 전환 · Language switch</summary>

한국어를 먼저 쓰고 영어를 바로 병기합니다. GitHub Markdown은 script/canvas를 실행하지 않으므로 native disclosure를 사용합니다. / Korean comes first with English immediately paired. GitHub Markdown cannot execute script/canvas, so this native disclosure is the supported switch.

</details>

## 잡음 속에서 신호 찾기 · Find the signal in the noise

임베디드 serial link를 위한 작은 byte-stream framing protocol입니다.
A tiny byte-stream framing protocol for an embedded serial link.

![language](https://img.shields.io/badge/language-C-555555?logo=c&logoColor=white)

## 프레임 구조 · Frame structure

```text
0x7E │ LEN │ payload (LEN bytes) │ checksum │ 0x7E
 ^                                              ^
start delimiter                         end / next start delimiter
checksum = XOR(all payload bytes)
```

`0x7E`와 `0x7D`는 byte-stuffing으로 escape합니다. 잡음 바이트는 시작 구분자 전까지 무시하고, 잘못된 checksum/length는 버립니다.
`0x7E` and `0x7D` are escaped with byte-stuffing. Noise before a start delimiter is ignored; invalid checksum/length frames are discarded.

## 실행·테스트 · Run and test

```sh
make test
```

테스트는 encode/decode 왕복, escaping, checksum 오류, 잡음이 섞인 stream 복구를 확인합니다.
Tests cover encode/decode round trips, escaping, checksum rejection, and recovery from noisy streams.

## 구조 · Structure

```text
src/frame.h   # format constants and parser state / 포맷 상수와 상태머신
src/frame.c   # encode_frame and frame_parser_feed
tests/test_frame.c
```

## 한계 · Boundaries

payload 최대 크기는 255 bytes입니다. 실제 UART register, interrupt, DMA, retransmission, ACK, timeout은 구현하지 않습니다.
Payload is limited to 255 bytes. UART registers, interrupts, DMA, retransmission, ACK, and timeout are not implemented.

## License

MIT © 2026 Seol
