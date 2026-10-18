/* 一致性套件入口，挂为 CTest 测试 `interface_conformance`。
 *
 * 层级：test
 * 职责：证明套件接受合规端口，并拒绝每个故意违规的端口。
 * 上下文：单线程测试程序。
 * 依赖：fw/conformance.h、fw/host.h、host_adapter.h、stdio.h、string.h。
 *
 * 本程序做两件事，第二件才是重点：
 *
 *   1. 对 ports/host 运行套件，必须通过。
 *   2. 对每个只违反一条契约的端口运行，确认套件点名该条款。
 *      一个从未拒绝过任何东西的套件没有证明力——它只是一个打印 PASS 的程序。
 */
#include "fw/conformance.h"
#include "fw/host.h"
#include "host_adapter.h"
#include <stdio.h>
#include <string.h>

static unsigned failures;

/* 记录失败但不中止：套件自测要报告所有不匹配，而不只是第一个。 */
static void expect(const char *what, bool condition)
{
    if (condition) return;
    printf("FAIL %s\n", what);
    ++failures;
}

/* 检查套件是否点名了我们故意注入违规的那条具体条款。 */
static bool clause_failed(const fw_conformance_result_t *result, const char *clause)
{
    unsigned i;
    for (i = 0U; i < result->failed_count; ++i) {
        if (strcmp(result->failed_clauses[i], clause) == 0) return true;
    }
    return false;
}

/* 一行 trace，让失败运行能识别是哪个端口变体。 */
static void summarize(const char *label, const fw_conformance_result_t *result)
{
    printf("  %-22s checks=%u failed=%u skipped=%u\n",
           label, result->checks, result->failed, result->skipped);
}

/* --- 故意违反 io.h 契约的 stream ------------------------------------------ */

static fw_status_t proxied_read(void *ctx, uint8_t *data, size_t capacity, size_t *count)
{
    fw_stream_t inner = fw_host_stream((fw_host_t *)ctx);
    return inner.ops->read(inner.ctx, data, capacity, count);
}

static fw_status_t proxied_write(void *ctx, const uint8_t *data, size_t length,
                                 size_t *count, uint32_t timeout_ms)
{
    fw_stream_t inner = fw_host_stream((fw_host_t *)ctx);
    return inner.ops->write(inner.ctx, data, length, count, timeout_ms);
}

/* 接受并存储字节，然后报告比调用者缓冲更大的 count。
 * io.h 明确禁止：调用者按 count 计算重试长度。 */
static fw_status_t overreporting_read(void *ctx, uint8_t *data, size_t capacity, size_t *count)
{
    fw_status_t status = proxied_read(ctx, data, capacity, count);
    if (*count > 0U) *count = capacity + 1U;
    return status;
}

/* 接受字节，却声称零进展。io.h：“接受了字节却报 0”属于违约。 */
static fw_status_t silent_write(void *ctx, const uint8_t *data, size_t length,
                                size_t *count, uint32_t timeout_ms)
{
    size_t accepted = 0U;
    (void)proxied_write(ctx, data, length, &accepted, timeout_ms);
    *count = 0U;
    return FW_OK;
}

/* 在调用者 capacity 之后多写一个字节。这是整个接口中后果最严重的条款：
 * 包装层可以夹紧说谎的 count，却无法撤销已经破坏的缓冲区，
 * 因此套件用 canary 检查，而不是只看 count。 */
static fw_status_t overrunning_read(void *ctx, uint8_t *data, size_t capacity, size_t *count)
{
    fw_status_t status = proxied_read(ctx, data, capacity, count);
    if (*count > 0U) data[capacity] = 0xAAU;
    return status;
}

static const fw_stream_ops_t overreporting_ops = { overreporting_read, proxied_write };
static const fw_stream_ops_t overrunning_ops = { overrunning_read, proxied_write };
static const fw_stream_ops_t silent_ops = { proxied_read, silent_write };

/* --- 倒退的时钟 ---------------------------------------------------------- */

static uint32_t regressing_now_ms(void *ctx)
{
    uint32_t *state = ctx;
    *state = (*state == 0U) ? UINT32_C(0x80000000) : 0U;
    return *state;
}

/* --- 报告成功但什么都不做的 indicator ------------------------------------ */

static fw_status_t oblivious_set(void *ctx, bool on)
{
    (void)ctx;
    (void)on;
    return FW_OK;
}

static const fw_indicator_ops_t oblivious_ops = { oblivious_set };

/* --- 测试 ----------------------------------------------------------------- */

/* 合规端口通过；每个故意破坏的代理都必须被点名。 */
static void test_stream_suite(void)
{
    fw_host_t host;
    fw_conformance_stream_io_t io;
    fw_conformance_result_t result;

    fw_host_init(&host);
    io = host_conformance_stream_io(&host);
    result = fw_conformance_stream(fw_host_stream(&host).ops, &host, &io);
    summarize("conforming port", &result);
    expect("a conforming port passes every stream clause", fw_conformance_ok(&result));

    fw_host_init(&host);
    io = host_conformance_stream_io(&host);
    result = fw_conformance_stream(&overreporting_ops, &host, &io);
    summarize("over-reporting read", &result);
    expect("over-reported read count is rejected", clause_failed(&result, "STREAM.READ.ORDER"));

    fw_host_init(&host);
    io = host_conformance_stream_io(&host);
    result = fw_conformance_stream(&overrunning_ops, &host, &io);
    summarize("overrunning read", &result);
    expect("a read that writes past capacity is caught by the canary",
           clause_failed(&result, "STREAM.READ.BOUNDED"));

    fw_host_init(&host);
    io = host_conformance_stream_io(&host);
    result = fw_conformance_stream(&silent_ops, &host, &io);
    summarize("silent write", &result);
    expect("write reporting zero progress is rejected",
           clause_failed(&result, "STREAM.WRITE.COUNT"));

    /* 没有故障 hook 的 harness 必须 SKIP，而不是失败：
     * 不能注入故障的端口仍应得到关于其余部分的答案。 */
    fw_host_init(&host);
    io = host_conformance_stream_io(&host);
    io.fault_read = NULL;
    io.fault_write = NULL;
    io.set_write_limit = NULL;
    result = fw_conformance_stream(fw_host_stream(&host).ops, &host, &io);
    summarize("conforming, no hooks", &result);
    expect("a port without fault hooks passes the remaining clauses and reports SKIP",
           fw_conformance_ok(&result) && result.skipped > 0U);
}

/* 套件必须拒绝“报告成功但状态不变”的 indicator。 */
static void test_indicator_suite(void)
{
    fw_host_t host;
    fw_conformance_indicator_io_t io;
    fw_conformance_result_t result;

    fw_host_init(&host);
    io = host_conformance_indicator_io(&host);
    result = fw_conformance_indicator(fw_host_indicator(&host).ops, &host, &io);
    summarize("conforming indicator", &result);
    expect("a conforming indicator passes every clause", fw_conformance_ok(&result));

    fw_host_init(&host);
    io = host_conformance_indicator_io(&host);
    result = fw_conformance_indicator(&oblivious_ops, &host, &io);
    summarize("oblivious indicator", &result);
    expect("an indicator that changes nothing is rejected",
           clause_failed(&result, "INDICATOR.STATE"));
}

/* 倒退的时钟必须被单调性条款拒绝。 */
static void test_clock_suite(void)
{
    fw_host_t host;
    fw_clock_t good;
    uint32_t state = 0U;
    fw_clock_t bad = { regressing_now_ms, &state };
    fw_conformance_result_t result;

    fw_host_init(&host);
    good = fw_host_clock(&host);
    result = fw_conformance_clock(&good);
    summarize("conforming clock", &result);
    expect("a conforming clock passes every clause", fw_conformance_ok(&result));

    result = fw_conformance_clock(&bad);
    summarize("regressing clock", &result);
    expect("a clock that jumps backwards is rejected", clause_failed(&result, "CLOCK.MONOTONIC"));
}

int main(void)
{
    test_stream_suite();
    test_indicator_suite();
    test_clock_suite();

    if (failures != 0U) {
        printf("CONFORMANCE SUITE SELF-TEST FAILED (%u)\n", failures);
        return 1;
    }
    printf("PASS conformance suite: accepts conforming ports, rejects every seeded "
           "violation, and reports SKIP rather than FAIL for absent hooks\n");
    return 0;
}
