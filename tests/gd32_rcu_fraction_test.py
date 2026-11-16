from pathlib import Path
import subprocess
import sys
import tempfile

source = (Path(__file__).resolve().parents[1] / "vendor/gd32f10x/2.7.0/GD32F10x_standard_peripheral/Source/gd32f10x_rcu.c").read_text(encoding="utf-8")
start = source.index("uint32_t rcu_clock_freq_get(")
opening = source.index("{", start)
depth = 0
for pos in range(opening, len(source)):
    if source[pos] == "{":
        depth += 1
    elif source[pos] == "}":
        depth -= 1
        if depth == 0:
            function = source[start:pos + 1]
            break
else:
    raise RuntimeError("clock frequency function is incomplete")

harness = r'''
#include <assert.h>
#include <stdint.h>
#define GD32F10X_CL 1
typedef enum { CK_SYS, CK_AHB, CK_APB1, CK_APB2 } rcu_clock_freq_enum;
static uint32_t cfg0,cfg1;
#define RCU_CFG0 cfg0
#define RCU_CFG1 cfg1
#define GET_BITS(v,a,b) (((v) >> (a)) & ((1U << ((b)-(a)+1U))-1U))
#define SEL_IRC8M 0U
#define SEL_HXTAL 1U
#define SEL_PLL 2U
#define IRC8M_VALUE 8000000U
#define HXTAL_VALUE 8000000U
#define RCU_CFG0_PLLSEL (1U<<16)
#define RCU_PLLSRC_HXTAL RCU_CFG0_PLLSEL
#define RCU_CFG0_PLLMF_4 (1U<<29)
#define RCU_CFG1_PREDV0 15U
#define RCU_CFG1_PREDV1 (15U<<4)
#define RCU_CFG1_PLL1MF (15U<<8)
#define RCU_CFG1_PREDV0SEL (1U<<16)
#define RCU_PREDV0SRC_CKPLL1 RCU_CFG1_PREDV0SEL
'''
harness += function
harness += r'''
int main(void)
{
    cfg0=(2U<<2)|RCU_CFG0_PLLSEL|(7U<<18);cfg1=2U;
    assert(rcu_clock_freq_get(CK_SYS)==24000000U);
    cfg0=(2U<<2)|RCU_CFG0_PLLSEL|(10U<<18)|RCU_CFG0_PLLMF_4|(4U<<8);
    assert(rcu_clock_freq_get(CK_SYS)==72000000U && rcu_clock_freq_get(CK_APB1)==36000000U);
    cfg0=(2U<<2)|RCU_CFG0_PLLSEL|(7U<<18);
    cfg1=RCU_CFG1_PREDV0SEL|4U|(2U<<4)|(13U<<8);
    assert(rcu_clock_freq_get(CK_SYS)==72000000U);
    cfg0=(2U<<2)|RCU_CFG0_PLLSEL|(13U<<18);cfg1=2U;
    assert(rcu_clock_freq_get(CK_SYS)==17333333U);
    cfg0=(2U<<2)|RCU_CFG0_PLLSEL|(10U<<18)|RCU_CFG0_PLLMF_4;
    cfg1=RCU_CFG1_PREDV0SEL|15U|(4U<<4)|(15U<<8);
    assert(rcu_clock_freq_get(CK_SYS)==54000000U); /* 8 MHz * 20 * 27 / 80 */
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="abox-gd-clock-") as directory:
    path = Path(directory)
    src, exe = path / "clock.c", path / "clock_test.exe"
    src.write_text(harness, encoding="utf-8", newline="\n")
    subprocess.run([sys.argv[1], "-std=c11", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
