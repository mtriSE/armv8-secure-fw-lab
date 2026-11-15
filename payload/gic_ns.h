#ifndef GIC_NS_H
#define GIC_NS_H
#include <stdint.h>
void gic_cpu_init(void);
void gic_enable_ppi(unsigned int intid, uint8_t prio);
uint32_t gic_ack(void);
void gic_eoi(uint32_t intid);
#endif
