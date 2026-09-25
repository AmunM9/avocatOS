#include "avo_core.h"

static avo_pwr_state_t sleep_state(const avo_pwr_t *p)
{
    return p->cfg.aod_enabled ? AVO_PWR_AOD : AVO_PWR_OFF;
}

void avo_pwr_init(avo_pwr_t *p, const avo_pwr_cfg_t *cfg, uint32_t now_ms)
{
    p->cfg = *cfg;
    p->state = AVO_PWR_ACTIVE;
    p->last_activity_ms = now_ms;
}

avo_pwr_state_t avo_pwr_activity(avo_pwr_t *p, uint32_t now_ms)
{
    p->last_activity_ms = now_ms;
    p->state = AVO_PWR_ACTIVE;
    return p->state;
}

avo_pwr_state_t avo_pwr_tick(avo_pwr_t *p, uint32_t now_ms)
{
    if (p->state == AVO_PWR_AOD || p->state == AVO_PWR_OFF) {
        return p->state;
    }
    /* unsigned subtraction handles the 32-bit tick wraparound */
    uint32_t idle = now_ms - p->last_activity_ms;
    if (idle >= p->cfg.sleep_after_ms) {
        p->state = sleep_state(p);
    } else if (idle >= p->cfg.dim_after_ms) {
        p->state = AVO_PWR_DIM;
    }
    return p->state;
}

avo_pwr_state_t avo_pwr_sleep(avo_pwr_t *p)
{
    p->state = sleep_state(p);
    return p->state;
}
