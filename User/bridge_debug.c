#include "bridge_debug.h"

#if BRIDGE_DEBUG_UART

#include "debug.h"
#include "usb_lib.h"
#include "usb_pwr.h"
#include "usb_host_config.h"
#include "ch32v20x.h"

extern struct _ROOT_HUB_DEVICE RootHubDev;
extern uint16_t Ep0RxBlks;

#define DBG_Q_SIZE  64

typedef struct
{
    uint8_t  ev;
    uint8_t  a;
    uint16_t b;
} BridgeDebugEvt;

static volatile uint8_t  g_q_w;
static volatile uint8_t  g_q_r;
static BridgeDebugEvt      g_q[DBG_Q_SIZE];

static uint32_t g_fwd_total;
static uint32_t g_fwd_sent;
static uint32_t g_stat_reset;
static uint32_t g_stat_err;
static uint32_t g_stat_setup;
static uint32_t g_stat_ctr_setup;
static uint32_t g_stat_desc;
static uint32_t g_stat_susp;
static volatile uint32_t g_sof_isr_cnt;
static uint8_t  g_last_pc_state;
static uint32_t g_tick;

static const char *BridgeDebug_PcStateName(uint8_t st)
{
    switch(st)
    {
        case 0: return "UNCONNECTED";
        case 1: return "ATTACHED";
        case 2: return "POWERED";
        case 3: return "SUSPENDED";
        case 4: return "ADDRESSED";
        case 5: return "CONFIGURED";
        default: return "?";
    }
}

void BridgeDebug_IsrPush(uint8_t ev, uint8_t arg0, uint16_t arg1)
{
    uint8_t n = (uint8_t)((g_q_w + 1U) % DBG_Q_SIZE);

    if(n == g_q_r)
    {
        return;
    }

    g_q[g_q_w].ev = ev;
    g_q[g_q_w].a  = arg0;
    g_q[g_q_w].b  = arg1;
    g_q_w = n;
}

static void BridgeDebug_HandleEvent(const BridgeDebugEvt *e)
{
    switch(e->ev)
    {
        case BRIDGE_EV_RESET:
            g_stat_reset++;
            printf("[PC] USB RESET #%lu  CNTR=0x%04X FNR=0x%04X\r\n",
                   (unsigned long)g_stat_reset,
                   (unsigned)_GetCNTR(), (unsigned)_GetFNR());
            break;

        case BRIDGE_EV_ERR:
            g_stat_err++;
            printf("[PC] USB ERR #%lu  ISTR=0x%04X\r\n",
                   (unsigned long)g_stat_err, (unsigned)_GetISTR());
            break;

        case BRIDGE_EV_CTR_SETUP:
            g_stat_ctr_setup++;
            if(g_stat_ctr_setup <= 20)
            {
                printf("[PC] CTR EP0 SETUP token #%lu\r\n",
                       (unsigned long)g_stat_ctr_setup);
            }
            break;

        case BRIDGE_EV_CTR_IN0:
            if(g_stat_setup <= 20)
            {
                printf("[PC] CTR EP0 IN\r\n");
            }
            break;

        case BRIDGE_EV_CTR_OUT0:
            if(g_stat_setup <= 20)
            {
                printf("[PC] CTR EP0 OUT\r\n");
            }
            break;

        case BRIDGE_EV_SUSP:
            g_stat_susp++;
            if(g_stat_susp <= 3U)
            {
                printf("[PC] USB SUSPEND #%lu (ignored until CONFIGURED)\r\n",
                       (unsigned long)g_stat_susp);
            }
            break;

        case BRIDGE_EV_WKUP:
            printf("[PC] USB WAKEUP\r\n");
            break;

        case BRIDGE_EV_SOF:
            break;

        case BRIDGE_EV_DOVR:
            printf("[PC] USB DMA/overrun DOVR\r\n");
            break;

        case BRIDGE_EV_SETUP:
            g_stat_setup++;
            printf("[PC] EP0 SETUP #%lu req=0x%02X wVal=0x%04X\r\n",
                   (unsigned long)g_stat_setup, e->a, e->b);
            break;

        case BRIDGE_EV_GET_DEV_DESC:
            g_stat_desc++;
            printf("[PC] GET_DEVICE_DESCRIPTOR #%lu\r\n", (unsigned long)g_stat_desc);
            break;

        case BRIDGE_EV_STATE:
            if(e->a != g_last_pc_state)
            {
                g_last_pc_state = e->a;
                printf("[PC] state=%u(%s)\r\n", e->a, BridgeDebug_PcStateName(e->a));
            }
            break;

        default:
            printf("[PC] evt=%u a=%u b=0x%04X\r\n", e->ev, e->a, e->b);
            break;
    }
}

void BridgeDebug_Flush(void)
{
    while(g_q_r != g_q_w)
    {
        BridgeDebugEvt e = g_q[g_q_r];
        g_q_r = (uint8_t)((g_q_r + 1U) % DBG_Q_SIZE);
        BridgeDebug_HandleEvent(&e);
    }
}

void BridgeDebug_Init(void)
{
    g_q_w = 0;
    g_q_r = 0;
    g_last_pc_state = 0xFF;

    USART_Printf_Init(BRIDGE_DEBUG_BAUD);
    printf("\r\n=== USB Mouse Bridge Debug ===\r\n");
    printf("UART PA9=TX GND common %lu 8N1\r\n", (unsigned long)BRIDGE_DEBUG_BAUD);
    printf("PC device: PA11=D- PA12=D+ (USBD, not swappable)\r\n");
    printf("Host port: PB6=D- PB7=D+ (starts after PC CONFIGURED)\r\n");
    printf("Logs are ISR-safe (queued, printed in main loop)\r\n\r\n");
}

void BridgeDebug_LogUsbInit(void)
{
    RCC_ClocksTypeDef clk = {0};
    uint32_t sws;

    RCC_GetClocksFreq(&clk);
    sws = RCC->CFGR0 & RCC_SWS;

    printf("[USB] init done\r\n");
    printf("[CLK] HSE_RDY=%u PLL_RDY=%u SWS=0x%02lX\r\n",
           (unsigned)((RCC->CTLR & RCC_HSERDY) ? 1U : 0U),
           (unsigned)((RCC->CTLR & RCC_PLLRDY) ? 1U : 0U),
           (unsigned long)sws);
    printf("[CLK] SYSCLK=%lu Hz PCLK1=%lu (need 96MHz for USB)\r\n",
           (unsigned long)clk.SYSCLK_Frequency,
           (unsigned long)clk.PCLK1_Frequency);

    if(clk.SYSCLK_Frequency != 96000000UL)
    {
        printf("[CLK] WARN: SYSCLK is not 96MHz - USB likely broken!\r\n");
        printf("[CLK] WARN: check 8MHz HSE crystal / PLL config\r\n");
    }
    if((RCC->CTLR & RCC_HSERDY) == 0)
    {
        printf("[CLK] WARN: HSE not ready - no external crystal?\r\n");
    }

    printf("[USB] CNTR=0x%04X ISTR=0x%04X IMR=0x%04X\r\n",
           (unsigned)_GetCNTR(), (unsigned)_GetISTR(), (unsigned)wInterrupt_Mask);
    printf("[USB] D+ pull-up=%s\r\n",
           ((EXTEN->EXTEN_CTR & EXTEN_USBD_PU_EN) != 0) ? "ON" : "OFF");
    printf("[USB] FNR=0x%04X RXDP=%u RXDM=%u\r\n",
           (unsigned)_GetFNR(),
           (unsigned)((_GetFNR() & FNR_RXDP) ? 1U : 0U),
           (unsigned)((_GetFNR() & FNR_RXDM) ? 1U : 0U));
    printf("[USB] EP0 RXcnt=0x%04X Ep0RxBlks=0x%04X\r\n",
           (unsigned)(*_pEPRxCount(0)),
           (unsigned)Ep0RxBlks);
    printf("[USB] wait for PC connect...\r\n\r\n");
}

void BridgeDebug_LogPcHostReady(void)
{
    printf("[PC] CONFIGURED -> starting USB Host on PB6/PB7\r\n");
}

void BridgeDebug_LogHostEnum(uint8_t ok, uint8_t err_code)
{
    if(ok)
    {
        printf("[Host] dongle enum OK speed=%u\r\n", RootHubDev.bSpeed);
    }
    else
    {
        printf("[Host] enum fail err=0x%02X\r\n", err_code);
    }
}

void BridgeDebug_LogMouseReady(uint8_t intf, uint16_t ep_size, uint8_t report_id)
{
    printf("[Host] mouse if=%u ep_max=%u report_id=%u\r\n", intf, ep_size, report_id);
}

void BridgeDebug_LogForward(const uint8_t *data, uint16_t len, uint8_t sent)
{
    uint16_t i;

    g_fwd_total++;
    if(sent)
    {
        g_fwd_sent++;
    }

    if((g_fwd_total % 50U) == 1U)
    {
        printf("[Fwd#%lu] len=%u sent=%s: ",
               (unsigned long)g_fwd_total, len, sent ? "yes" : "no");
        for(i = 0; i < len && i < 8; i++)
        {
            printf("%02X ", data[i]);
        }
        printf("\r\n");
    }
}

void BridgeDebug_Tick(void)
{
    uint32_t sof_cnt;

    g_tick++;

    sof_cnt = g_sof_isr_cnt;
    if((sof_cnt > 0U) && ((sof_cnt % 500U) == 1U))
    {
        printf("[PC] SOF x%lu (USB bus active)\r\n", (unsigned long)sof_cnt);
    }

    if((g_tick % 500000U) != 0U)
    {
        return;
    }

    printf("[Stat] pc=%u(%s) host=%u rst=%lu ctr=%lu setup=%lu desc=%lu err=%lu sof=%lu susp=%lu\r\n",
           (unsigned)bDeviceState,
           BridgeDebug_PcStateName(bDeviceState),
           RootHubDev.bStatus,
           (unsigned long)g_stat_reset,
           (unsigned long)g_stat_ctr_setup,
           (unsigned long)g_stat_setup,
           (unsigned long)g_stat_desc,
           (unsigned long)g_stat_err,
           (unsigned long)sof_cnt,
           (unsigned long)g_stat_susp);

    if(g_stat_reset >= 2U && g_stat_ctr_setup == 0U && g_stat_setup == 0U)
    {
        printf("[Diag] RESET ok but no EP0 CTR/SETUP -> check USB clk, pull-up, signal integrity\r\n");
    }
    else if(g_stat_ctr_setup > 0U && g_stat_setup == 0U)
    {
        printf("[Diag] CTR SETUP seen but Setup0_Process not reached -> EP0 handler issue\r\n");
    }
    else if(g_stat_setup > 0U && g_stat_desc == 0U)
    {
        printf("[Diag] SETUP ok but no GET_DEVICE_DESCRIPTOR -> request parse issue\r\n");
    }
}

void RESET_Callback(void)
{
    BridgeDebug_IsrPush(BRIDGE_EV_RESET, 0, 0);
}

void ERR_Callback(void)
{
    BridgeDebug_IsrPush(BRIDGE_EV_ERR, 0, 0);
}

void SUSP_Callback(void)
{
    BridgeDebug_IsrPush(BRIDGE_EV_SUSP, 0, 0);
}

void SOF_Callback(void)
{
    g_sof_isr_cnt++;
}

void DOVR_Callback(void)
{
    BridgeDebug_IsrPush(BRIDGE_EV_DOVR, 0, 0);
}

void WKUP_Callback(void)
{
    BridgeDebug_IsrPush(BRIDGE_EV_WKUP, 0, 0);
}

#else

void RESET_Callback(void) {}
void ERR_Callback(void) {}
void SUSP_Callback(void) {}
void SOF_Callback(void) {}
void DOVR_Callback(void) {}
void WKUP_Callback(void) {}

#endif /* BRIDGE_DEBUG_UART */
