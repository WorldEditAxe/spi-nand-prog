#include <spi-mem-drvs.h>
#include <stdio.h>
#include <string.h>

struct spi_mem *spi_mem_probe(const char *drv, const char *drvarg) {
    if (!strcmp(drv, "ch347"))
        return ch347_probe();
    if (!strcmp(drv, "fx2qspi"))
        return fx2qspi_probe();
    if (!strcmp(drv, "serprog"))
        return serprog_probe(drvarg);
#ifdef __linux__
    if (!strcmp(drv, "spidev") || !strcmp(drv, "linux-spi") ||
        !strcmp(drv, "linux"))
        return linux_spi_probe(drvarg);
#endif
    return NULL;
}

void spi_mem_remove(const char *drv, struct spi_mem *mem) {
    if (!strcmp(drv, "ch347"))
        return ch347_remove(mem);
    if (!strcmp(drv, "fx2qspi"))
        return fx2qspi_remove(mem);
    if (!strcmp(drv, "serprog"))
        return serprog_remove(mem);
#ifdef __linux__
    if (!strcmp(drv, "spidev") || !strcmp(drv, "linux-spi") ||
        !strcmp(drv, "linux"))
        return linux_spi_remove(mem);
#endif
}
