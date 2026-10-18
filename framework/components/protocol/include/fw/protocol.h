/*
 * fw/protocol.h - 带 CRC-16 的二进制帧协议。
 *
 * 层级：components/protocol
 * 职责：把消息编码成字节，并从字节流中恢复消息。
 * 上下文：parser 状态由单任务拥有；无内部锁。
 * 依赖：fw/status.h、stdbool.h、stddef.h、stdint.h。
 *
 * 帧格式：A5 5A LEN SEQ CMD PAYLOAD CRC_LO CRC_HI
 * CRC-16/CCITT-FALSE 覆盖 LEN、SEQ、CMD 和 PAYLOAD，不覆盖前导码。
 */
#ifndef FW_PROTOCOL_H
#define FW_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fw/status.h"

#define FW_PROTOCOL_MAX_PAYLOAD 32U
#define FW_PROTOCOL_MAX_FRAME (FW_PROTOCOL_MAX_PAYLOAD + 7U)

typedef struct {
    uint8_t sequence;  /* 仅用于关联；回复沿用请求值 */
    uint8_t command;   /* bit7=0 请求，bit7=1 回复/事件 */
    uint8_t length;    /* payload 长度，0..FW_PROTOCOL_MAX_PAYLOAD */
    uint8_t payload[FW_PROTOCOL_MAX_PAYLOAD];  /* 复制存储，不引用外部指针 */
} fw_message_t;

typedef struct {
    uint8_t state;    /* parser 内部状态；外部应视为不透明 */
    uint8_t body[FW_PROTOCOL_MAX_PAYLOAD + 3U];  /* LEN + SEQ + CMD + payload */
    size_t used;      /* 当前帧已存入 body 的字节数 */
    uint16_t received_crc;  /* 先低字节后高字节拼出的 CRC */
    uint32_t bad_crc;       /* 诊断计数；reset 后保留 */
    uint32_t bad_length;    /* 诊断计数；reset 后保留 */
} fw_parser_t;

/* 初始化 parser 状态和计数器；传入 NULL 时安全无操作。 */
void fw_parser_init(fw_parser_t *parser);

/* 只复位未完成帧的状态；诊断计数器保留。 */
void fw_parser_reset(fw_parser_t *parser);

/* 当前已开始一帧但尚未完成或超时时返回 true。 */
bool fw_parser_partial(const fw_parser_t *parser);

/* 送入一个字节。仅当一帧合法完成时返回 true，并把 payload 复制到
 * *message。噪声、CRC/长度错误会被消费并计数；parser 在下一个 A5 5A
 * 处重新同步。 */
bool fw_parser_push(fw_parser_t *parser, uint8_t byte, fw_message_t *message);

/* CRC-16/CCITT-FALSE：init=FFFF、poly=1021、不反射、xorout=0。 */
uint16_t fw_crc16(const uint8_t *data, size_t length);

/* 编码一条消息。capacity 不足时返回 FW_EOVERFLOW，并把 *length 置 0。
 * payload 会被复制，不保留任何指针。 */
fw_status_t fw_protocol_encode(const fw_message_t *message, uint8_t *output,
                               size_t capacity, size_t *length);

#endif
