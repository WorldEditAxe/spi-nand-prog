#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/spi/spidev.h>

#include <spi.h>
#include <spi-mem.h>

#ifndef SPI_NBITS_SINGLE
#define SPI_NBITS_SINGLE 0x01
#endif
#ifndef SPI_NBITS_DUAL
#define SPI_NBITS_DUAL 0x02
#endif
#ifndef SPI_NBITS_QUAD
#define SPI_NBITS_QUAD 0x04
#endif

#define LINUX_SPI_DEFAULT_BITS_PER_WORD 8
#define LINUX_SPI_DEFAULT_MAX_TRANSFER 4096

struct linux_spi_priv {
	int fd;
	u32 mode;
	u32 io_caps;
	u32 speed_hz;
	u32 max_transfer;
	u16 delay_usecs;
	u8 bits_per_word;
};

static int linux_spi_parse_u32(const char *str, u32 *val)
{
	char *endptr;
	unsigned long tmp;

	errno = 0;
	tmp = strtoul(str, &endptr, 0);
	if (errno || !str[0] || *endptr || tmp > UINT32_MAX)
		return -EINVAL;

	*val = tmp;
	return 0;
}

static int linux_spi_parse_u16(const char *str, u16 *val)
{
	u32 tmp;
	int ret;

	ret = linux_spi_parse_u32(str, &tmp);
	if (ret)
		return ret;

	if (tmp > UINT16_MAX)
		return -EINVAL;

	*val = tmp;
	return 0;
}

static int linux_spi_parse_u8(const char *str, u8 *val)
{
	u32 tmp;
	int ret;

	ret = linux_spi_parse_u32(str, &tmp);
	if (ret)
		return ret;

	if (tmp > UCHAR_MAX)
		return -EINVAL;

	*val = tmp;
	return 0;
}

static int linux_spi_parse_io(const char *str, u32 *io_caps)
{
	if (!strcmp(str, "single") || !strcmp(str, "1")) {
		*io_caps = 0;
		return 0;
	}

	if (!strcmp(str, "dual") || !strcmp(str, "2")) {
		*io_caps = SPI_TX_DUAL | SPI_RX_DUAL;
		return 0;
	}

	if (!strcmp(str, "rx-dual") || !strcmp(str, "rx2")) {
		*io_caps = SPI_RX_DUAL;
		return 0;
	}

	if (!strcmp(str, "quad") || !strcmp(str, "4")) {
		*io_caps = SPI_TX_DUAL | SPI_RX_DUAL |
			   SPI_TX_QUAD | SPI_RX_QUAD;
		return 0;
	}

	if (!strcmp(str, "rx-quad") || !strcmp(str, "rx4")) {
		*io_caps = SPI_RX_DUAL | SPI_RX_QUAD;
		return 0;
	}

	fprintf(stderr, "linux-spi: unsupported io mode '%s'.\n", str);
	return -EINVAL;
}

static int linux_spi_parse_arg(const char *drvarg, struct linux_spi_priv *priv,
			       char **devpath)
{
	char *arg, *saveptr = NULL, *tok;
	int ret = 0;

	if (!drvarg || !drvarg[0]) {
		fprintf(stderr,
			"linux-spi: missing device path; use -a /dev/spidevX.Y\n");
		return -EINVAL;
	}

	arg = strdup(drvarg);
	if (!arg)
		return -ENOMEM;

	tok = strtok_r(arg, ",", &saveptr);
	if (!tok || !tok[0]) {
		ret = -EINVAL;
		goto out;
	}

	*devpath = strdup(tok);
	if (!*devpath) {
		ret = -ENOMEM;
		goto out;
	}

	while ((tok = strtok_r(NULL, ",", &saveptr))) {
		char *key, *val;

		key = tok;
		val = strchr(tok, '=');
		if (!val) {
			fprintf(stderr,
				"linux-spi: expected key=value option, got '%s'.\n",
				tok);
			ret = -EINVAL;
			goto out;
		}

		*val++ = '\0';
		if (!strcmp(key, "speed") || !strcmp(key, "speed_hz")) {
			ret = linux_spi_parse_u32(val, &priv->speed_hz);
		} else if (!strcmp(key, "mode")) {
			ret = linux_spi_parse_u32(val, &priv->mode);
		} else if (!strcmp(key, "bpw") ||
			   !strcmp(key, "bits_per_word")) {
			ret = linux_spi_parse_u8(val, &priv->bits_per_word);
		} else if (!strcmp(key, "delay") ||
			   !strcmp(key, "delay_usecs")) {
			ret = linux_spi_parse_u16(val, &priv->delay_usecs);
		} else if (!strcmp(key, "io")) {
			ret = linux_spi_parse_io(val, &priv->io_caps);
		} else if (!strcmp(key, "max") ||
			   !strcmp(key, "max_transfer")) {
			ret = linux_spi_parse_u32(val, &priv->max_transfer);
			if (!ret && !priv->max_transfer)
				ret = -EINVAL;
		} else {
			fprintf(stderr, "linux-spi: unknown option '%s'.\n",
				key);
			ret = -EINVAL;
		}

		if (ret)
			goto out;
	}

out:
	free(arg);
	if (ret) {
		free(*devpath);
		*devpath = NULL;
	}
	return ret;
}

static int linux_spi_configure(struct linux_spi_priv *priv)
{
	u32 mode32 = priv->mode | priv->io_caps;
	int err, ret;

#ifdef SPI_IOC_WR_MODE32
	ret = ioctl(priv->fd, SPI_IOC_WR_MODE32, &mode32);
#else
	if (mode32 > UCHAR_MAX) {
		fprintf(stderr,
			"linux-spi: this system lacks SPI_IOC_WR_MODE32 for multi-I/O flags.\n");
		return -EOPNOTSUPP;
	}
	{
		u8 mode8 = mode32;
		ret = ioctl(priv->fd, SPI_IOC_WR_MODE, &mode8);
	}
#endif
	if (ret < 0) {
		err = errno;
		perror("linux-spi: set mode");
		return -err;
	}

	ret = ioctl(priv->fd, SPI_IOC_WR_BITS_PER_WORD,
		    &priv->bits_per_word);
	if (ret < 0) {
		err = errno;
		perror("linux-spi: set bits per word");
		return -err;
	}

	if (priv->speed_hz) {
		ret = ioctl(priv->fd, SPI_IOC_WR_MAX_SPEED_HZ,
			    &priv->speed_hz);
		if (ret < 0) {
			err = errno;
			perror("linux-spi: set max speed");
			return -err;
		}
	} else {
		u32 speed_hz = 0;

		ret = ioctl(priv->fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed_hz);
		if (!ret)
			priv->speed_hz = speed_hz;
	}

	return 0;
}

static int linux_spi_adjust_op_size(struct spi_mem *mem,
				    struct spi_mem_op *op)
{
	struct linux_spi_priv *priv = spi_mem_get_drvdata(mem);
	size_t max_data = priv->max_transfer;

	if (op->data.dir == SPI_MEM_DATA_OUT) {
		size_t overhead = 1 + op->addr.nbytes + op->dummy.nbytes;

		if (overhead >= priv->max_transfer)
			return -E2BIG;

		max_data -= overhead;
	}

	if (op->data.nbytes > max_data)
		op->data.nbytes = max_data;

	return 0;
}

static bool linux_spi_buswidth_is_supported(u8 buswidth)
{
	return buswidth == 1 || buswidth == 2 || buswidth == 4;
}

static bool linux_spi_supports_op(struct spi_mem *mem,
				  const struct spi_mem_op *op)
{
	if (!linux_spi_buswidth_is_supported(op->cmd.buswidth))
		return false;

	if (op->cmd.buswidth != 1)
		return false;

	if (op->addr.nbytes &&
	    (!linux_spi_buswidth_is_supported(op->addr.buswidth) ||
	     op->addr.nbytes > sizeof(u64)))
		return false;

	if (op->dummy.nbytes &&
	    !linux_spi_buswidth_is_supported(op->dummy.buswidth))
		return false;

	if (op->data.nbytes &&
	    !linux_spi_buswidth_is_supported(op->data.buswidth))
		return false;

	return spi_mem_default_supports_op(mem, op);
}

static u8 linux_spi_nbits(u8 buswidth)
{
	switch (buswidth) {
	case 4:
		return SPI_NBITS_QUAD;
	case 2:
		return SPI_NBITS_DUAL;
	default:
		return SPI_NBITS_SINGLE;
	}
}

static void linux_spi_init_xfer(struct linux_spi_priv *priv,
				struct spi_ioc_transfer *xfer, u32 len)
{
	memset(xfer, 0, sizeof(*xfer));
	xfer->len = len;
	xfer->speed_hz = priv->speed_hz;
	xfer->delay_usecs = priv->delay_usecs;
	xfer->bits_per_word = priv->bits_per_word;
}

static int linux_spi_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct linux_spi_priv *priv = spi_mem_get_drvdata(mem);
	struct spi_ioc_transfer xfers[4];
	unsigned int nxfers = 0;
	u8 cmd = op->cmd.opcode;
	u8 addr[sizeof(u64)];
	u8 dummy[UCHAR_MAX] = { };
	int err, i, ret;

	linux_spi_init_xfer(priv, &xfers[nxfers], 1);
	xfers[nxfers].tx_buf = (uintptr_t)&cmd;
	xfers[nxfers].tx_nbits = linux_spi_nbits(op->cmd.buswidth);
	nxfers++;

	if (op->addr.nbytes) {
		for (i = op->addr.nbytes - 1; i >= 0; i--)
			addr[op->addr.nbytes - 1 - i] =
				(op->addr.val >> (i * 8)) & 0xff;

		linux_spi_init_xfer(priv, &xfers[nxfers], op->addr.nbytes);
		xfers[nxfers].tx_buf = (uintptr_t)addr;
		xfers[nxfers].tx_nbits = linux_spi_nbits(op->addr.buswidth);
		nxfers++;
	}

	if (op->dummy.nbytes) {
		linux_spi_init_xfer(priv, &xfers[nxfers], op->dummy.nbytes);
		xfers[nxfers].tx_buf = (uintptr_t)dummy;
		xfers[nxfers].tx_nbits = linux_spi_nbits(op->dummy.buswidth);
		nxfers++;
	}

	if (op->data.nbytes) {
		linux_spi_init_xfer(priv, &xfers[nxfers], op->data.nbytes);
		if (op->data.dir == SPI_MEM_DATA_OUT) {
			xfers[nxfers].tx_buf = (uintptr_t)op->data.buf.out;
			xfers[nxfers].tx_nbits =
				linux_spi_nbits(op->data.buswidth);
		} else if (op->data.dir == SPI_MEM_DATA_IN) {
			xfers[nxfers].rx_buf = (uintptr_t)op->data.buf.in;
			xfers[nxfers].rx_nbits =
				linux_spi_nbits(op->data.buswidth);
		} else {
			return -EINVAL;
		}
		nxfers++;
	}

	ret = ioctl(priv->fd, SPI_IOC_MESSAGE(nxfers), xfers);
	if (ret < 0) {
		err = errno;
		perror("linux-spi: transfer");
		return -err;
	}

	return 0;
}

static const struct spi_controller_mem_ops linux_spi_mem_ops = {
	.adjust_op_size = linux_spi_adjust_op_size,
	.supports_op = linux_spi_supports_op,
	.exec_op = linux_spi_exec_op,
};

struct spi_mem *linux_spi_probe(const char *drvarg)
{
	struct linux_spi_priv *priv;
	struct spi_mem *mem;
	char *devpath = NULL;
	int ret;

	priv = calloc(1, sizeof(*priv));
	if (!priv)
		return NULL;

	priv->fd = -1;
	priv->bits_per_word = LINUX_SPI_DEFAULT_BITS_PER_WORD;
	priv->max_transfer = LINUX_SPI_DEFAULT_MAX_TRANSFER;

	ret = linux_spi_parse_arg(drvarg, priv, &devpath);
	if (ret)
		goto err_free_priv;

	priv->fd = open(devpath, O_RDWR);
	if (priv->fd < 0) {
		perror("linux-spi: open");
		goto err_free_devpath;
	}

	ret = linux_spi_configure(priv);
	if (ret)
		goto err_close;

	mem = calloc(1, sizeof(*mem));
	if (!mem)
		goto err_close;

	mem->ops = &linux_spi_mem_ops;
	mem->spi_mode = priv->io_caps;
	mem->name = "linux-spi";
	mem->drvpriv = priv;

	if (priv->speed_hz)
		printf("linux-spi: using %s at %u Hz, mode 0x%x, %u bpw.\n",
		       devpath, priv->speed_hz, priv->mode | priv->io_caps,
		       priv->bits_per_word);
	else
		printf("linux-spi: using %s, mode 0x%x, %u bpw.\n",
		       devpath, priv->mode | priv->io_caps,
		       priv->bits_per_word);

	free(devpath);
	return mem;

err_close:
	close(priv->fd);
err_free_devpath:
	free(devpath);
err_free_priv:
	free(priv);
	return NULL;
}

void linux_spi_remove(struct spi_mem *mem)
{
	struct linux_spi_priv *priv;

	if (!mem)
		return;

	priv = spi_mem_get_drvdata(mem);
	if (priv) {
		if (priv->fd >= 0)
			close(priv->fd);
		free(priv);
	}
	free(mem);
}
