/*
 * fw/protocol.c - 逐字节帧解析与 CRC 编码。
 *
 * 层级：components/protocol
 * 职责：为单条 UART 字节流提供有界、无内存分配的组帧。
 * 上下文：parser 由单所有者使用；调用者负责串行化。
 * 依赖：fw/protocol.h、string.h。
 */
#include "fw/protocol.h"
#include <string.h>

/* parser 状态按线路顺序排列。保持私有，避免调用者依赖内部状态机；
 * 公开查询只有 fw_parser_partial()。 */
enum { SEEK_A5, SEEK_5A, READ_LENGTH, READ_BODY, READ_CRC_LO, READ_CRC_HI };

/* -------------------------------------------------------------------------
 * CRC 校验
 * ------------------------------------------------------------------------- */
uint16_t fw_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = UINT16_C(0xffff);
    size_t i;
    unsigned bit;
    for (i = 0U; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (bit = 0U; bit < 8U; ++bit) {
            /* 逐位实现让代码更小、结果确定；一帧最多 39 字节，
             * 这里不追求速度。 */
            crc = (uint16_t)((crc & UINT16_C(0x8000)) != 0U
                  ? ((uint32_t)crc << 1U) ^ UINT32_C(0x1021)
                  : (uint32_t)crc << 1U);
        }
    }
    return crc;
}

/* -------------------------------------------------------------------------
 * 解析器
 * ------------------------------------------------------------------------- */
void fw_parser_init(fw_parser_t *parser)
{
    if (parser != NULL) memset(parser, 0, sizeof(*parser));
}

void fw_parser_reset(fw_parser_t *parser)
{
    if (parser != NULL) {
        parser->state = SEEK_A5;
        parser->used = 0U;
        parser->received_crc = 0U;
        /* bad_crc/bad_length 故意保留：它们描述整条流，不属于当前半帧。 */
    }
}

bool fw_parser_partial(const fw_parser_t *parser)
{
    return parser != NULL && parser->state != SEEK_A5;
}

bool fw_parser_push(fw_parser_t *parser, uint8_t byte, fw_message_t *message)
{
    if (parser == NULL || message == NULL) return false;
    switch (parser->state) {
    case SEEK_A5:
        if (byte == 0xa5U) parser->state = SEEK_5A;
        break;
    case SEEK_5A:
        /* 重复的 A5 可能是新帧前导码，不应视为错误。 */
        parser->state = byte == 0x5aU ? READ_LENGTH :
                        (byte == 0xa5U ? SEEK_5A : SEEK_A5);
        break;
    case READ_LENGTH:
        if (byte > FW_PROTOCOL_MAX_PAYLOAD) {
            ++parser->bad_length;
            fw_parser_reset(parser);
            /* 如果被拒绝的长度字节本身是 A5，它也可能是下一帧前导码的首字节。 */
            if (byte == 0xa5U) parser->state = SEEK_5A;
        } else {
            parser->body[0] = byte;
            parser->used = 1U;
            parser->state = READ_BODY;
        }
        break;
    case READ_BODY:
        parser->body[parser->used++] = byte;
        /* body[0] 是 LEN；再加上 SEQ 和 CMD 两个字节。 */
        if (parser->used == (size_t)parser->body[0] + 3U) {
            parser->state = READ_CRC_LO;
        }
        break;
    case READ_CRC_LO:
        parser->received_crc = byte;
        parser->state = READ_CRC_HI;
        break;
    case READ_CRC_HI:
        parser->received_crc |= (uint16_t)((uint16_t)byte << 8U);
        if (parser->received_crc == fw_crc16(parser->body, parser->used)) {
            message->length = parser->body[0];
            message->sequence = parser->body[1];
            message->command = parser->body[2];
            memcpy(message->payload, &parser->body[3], message->length);
            fw_parser_reset(parser);
            return true;
        }
        ++parser->bad_crc;
        fw_parser_reset(parser);
        break;
    default:
        /* 状态损坏也可恢复：重新寻找前导码。 */
        fw_parser_reset(parser);
        break;
    }
    return false;
}

/* -------------------------------------------------------------------------
 * 编码
 * ------------------------------------------------------------------------- */
fw_status_t fw_protocol_encode(const fw_message_t *message, uint8_t *output,
                               size_t capacity, size_t *length)
{
    uint16_t crc;
    size_t total;
    if (length == NULL) return FW_EINVAL;
    *length = 0U;
    if (message == NULL || output == NULL ||
        message->length > FW_PROTOCOL_MAX_PAYLOAD) return FW_EINVAL;
    total = (size_t)message->length + 7U;
    if (capacity < total) return FW_EOVERFLOW;
    output[0] = 0xa5U;
    output[1] = 0x5aU;
    output[2] = message->length;
    output[3] = message->sequence;
    output[4] = message->command;
    memcpy(&output[5], message->payload, message->length);
    /* CRC 覆盖 LEN 到 payload 末尾。 */
    crc = fw_crc16(&output[2], (size_t)message->length + 3U);
    output[total - 2U] = (uint8_t)crc;
    output[total - 1U] = (uint8_t)(crc >> 8U);
    *length = total;
    return FW_OK;
}
