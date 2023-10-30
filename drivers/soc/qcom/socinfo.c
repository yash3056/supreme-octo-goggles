// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2009-2017, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2019, Linaro Ltd.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/soc/qcom/smem.h>
#include <linux/string.h>
#include <linux/sys_soc.h>
#include <linux/types.h>
#include <linux/of.h>
#include <soc/qcom/socinfo.h>
#include <asm/unaligned.h>

/*
 * SoC version type with major number in the upper 16 bits and minor
 * number in the lower 16 bits.
 */
#define SOCINFO_MAJOR(ver) (((ver) >> 16) & 0xffff)
#define SOCINFO_MINOR(ver) ((ver) & 0xffff)
#define SOCINFO_VERSION(maj, min)  ((((maj) & 0xffff) << 16)|((min) & 0xffff))

#define SMEM_SOCINFO_BUILD_ID_LENGTH           32
#define SMEM_SOCINFO_CHIP_ID_LENGTH            32

/*
 * SMEM item id, used to acquire handles to respective
 * SMEM region.
 */
#define SMEM_HW_SW_BUILD_ID            137

#ifdef CONFIG_DEBUG_FS
#define SMEM_IMAGE_VERSION_BLOCKS_COUNT        32
#define SMEM_IMAGE_VERSION_SIZE                4096
#define SMEM_IMAGE_VERSION_NAME_SIZE           75
#define SMEM_IMAGE_VERSION_VARIANT_SIZE        20
#define SMEM_IMAGE_VERSION_OEM_SIZE            32

/*
 * SMEM Image table indices
 */
#define SMEM_IMAGE_TABLE_BOOT_INDEX     0
#define SMEM_IMAGE_TABLE_TZ_INDEX       1
#define SMEM_IMAGE_TABLE_RPM_INDEX      3
#define SMEM_IMAGE_TABLE_APPS_INDEX     10
#define SMEM_IMAGE_TABLE_MPSS_INDEX     11
#define SMEM_IMAGE_TABLE_ADSP_INDEX     12
#define SMEM_IMAGE_TABLE_CNSS_INDEX     13
#define SMEM_IMAGE_TABLE_VIDEO_INDEX    14
#define SMEM_IMAGE_VERSION_TABLE       469

/*
 * SMEM Image table names
 */
static const char *const socinfo_image_names[] = {
	[SMEM_IMAGE_TABLE_ADSP_INDEX] = "adsp",
	[SMEM_IMAGE_TABLE_APPS_INDEX] = "apps",
	[SMEM_IMAGE_TABLE_BOOT_INDEX] = "boot",
	[SMEM_IMAGE_TABLE_CNSS_INDEX] = "cnss",
	[SMEM_IMAGE_TABLE_MPSS_INDEX] = "mpss",
	[SMEM_IMAGE_TABLE_RPM_INDEX] = "rpm",
	[SMEM_IMAGE_TABLE_TZ_INDEX] = "tz",
	[SMEM_IMAGE_TABLE_VIDEO_INDEX] = "video",
};

static const char *const pmic_models[] = {
	[0]  = "Unknown PMIC model",
	[9]  = "PM8994",
	[11] = "PM8916",
	[13] = "PM8058",
	[14] = "PM8028",
	[15] = "PM8901",
	[16] = "PM8027",
	[17] = "ISL9519",
	[18] = "PM8921",
	[19] = "PM8018",
	[20] = "PM8015",
	[21] = "PM8014",
	[22] = "PM8821",
	[23] = "PM8038",
	[24] = "PM8922",
	[25] = "PM8917",
};
#endif /* CONFIG_DEBUG_FS */

/* Socinfo SMEM item structure */
struct socinfo {
	__le32 fmt;
	__le32 id;
	__le32 ver;
	char build_id[SMEM_SOCINFO_BUILD_ID_LENGTH];
	/* Version 2 */
	__le32 raw_id;
	__le32 raw_ver;
	/* Version 3 */
	__le32 hw_plat;
	/* Version 4 */
	__le32 plat_ver;
	/* Version 5 */
	__le32 accessory_chip;
	/* Version 6 */
	__le32 hw_plat_subtype;
	/* Version 7 */
	__le32 pmic_model;
	__le32 pmic_die_rev;
	/* Version 8 */
	__le32 pmic_model_1;
	__le32 pmic_die_rev_1;
	__le32 pmic_model_2;
	__le32 pmic_die_rev_2;
	/* Version 9 */
	__le32 foundry_id;
	/* Version 10 */
	__le32 serial_num;
	/* Version 11 */
	__le32 num_pmics;
	__le32 pmic_array_offset;
	/* Version 12 */
	__le32 chip_family;
	__le32 raw_device_family;
	__le32 raw_device_num;
	/* Version 13 */
	__le32 nproduct_id;
	char chip_id[SMEM_SOCINFO_CHIP_ID_LENGTH];
	/* Version 14 */
	__le32 num_clusters;
	__le32 ncluster_array_offset;
	__le32 num_defective_parts;
	__le32 ndefective_parts_array_offset;
	/* Version 15 */
	__le32  nmodem_supported;
	/* Version 16 */
	__le32  esku;
	__le32  nproduct_code;
	__le32  npartnamemap_offset;
	__le32  nnum_partname_mapping;
} *socinfo;

static const char *machine_name_buf = NULL;

/* sysfs attributes */
#define ATTR_DEFINE(param)	\
	static DEVICE_ATTR(param, 0644,	\
		   msm_get_##param,	\
		   NULL)

#define BUILD_ID_LENGTH 32
#define CHIP_ID_LENGTH 32
#define SMEM_IMAGE_VERSION_BLOCKS_COUNT 32
#define SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE 128
#define SMEM_IMAGE_VERSION_SIZE 4096
#define SMEM_IMAGE_VERSION_NAME_SIZE 75
#define SMEM_IMAGE_VERSION_VARIANT_SIZE 20
#define SMEM_IMAGE_VERSION_VARIANT_OFFSET 75
#define SMEM_IMAGE_VERSION_OEM_SIZE 33
#define SMEM_IMAGE_VERSION_OEM_OFFSET 95
#define SMEM_IMAGE_VERSION_PARTITION_APPS 10
#define SMEM_IMAGE_MACHINE_NAME_SIZE 75

/* Version 2 */
static uint32_t socinfo_get_raw_id(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 2) ?
			le32_to_cpu(socinfo->raw_id) : 0)
		: 0;
}

static uint32_t socinfo_get_raw_version(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 2) ?
			le32_to_cpu(socinfo->raw_ver) : 0)
		: 0;
}

/* Version 3 */
static uint32_t socinfo_get_platform_type(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 3) ?
			le32_to_cpu(socinfo->hw_plat) : 0)
		: 0;
}

/* Version 4 */
static uint32_t socinfo_get_platform_version(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 4) ?
			le32_to_cpu(socinfo->plat_ver) : 0)
		: 0;
}
/* Version 5 */
static uint32_t socinfo_get_accessory_chip(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 5) ?
			le32_to_cpu(socinfo->accessory_chip) : 0)
		: 0;
}

/* Version 6 */
static uint32_t socinfo_get_platform_subtype(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 6) ?
			le32_to_cpu(socinfo->hw_plat_subtype) : 0)
		: 0;
}

/* Version 7 */
static int socinfo_get_pmic_model(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 7) ?
			le32_to_cpu(socinfo->pmic_model) : 0xFFFFFFFF)
		: 0xFFFFFFFF;
}

static uint32_t socinfo_get_pmic_die_revision(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 7) ?
			le32_to_cpu(socinfo->pmic_die_rev) : 0)
		: 0;
}

/* Version 9 */
static uint32_t socinfo_get_foundry_id(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 9) ?
			le32_to_cpu(socinfo->foundry_id) : 0)
		: 0;
}

/* Version 10 */
uint32_t socinfo_get_serial_number(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 10) ?
			le32_to_cpu(socinfo->serial_num) : 0)
		: 0;
}
EXPORT_SYMBOL(socinfo_get_serial_number);

/* Version 12 */
static uint32_t socinfo_get_chip_family(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 12) ?
			le32_to_cpu(socinfo->chip_family) : 0)
		: 0;
}

static uint32_t socinfo_get_raw_device_family(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 12) ?
			le32_to_cpu(socinfo->raw_device_family) : 0)
		: 0;
}

static uint32_t socinfo_get_raw_device_number(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 12) ?
			le32_to_cpu(socinfo->raw_device_num) : 0)
		: 0;
}

/* Version 13 */
static uint32_t socinfo_get_nproduct_id(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 13) ?
			le32_to_cpu(socinfo->nproduct_id) : 0)
		: 0;
}

static char *socinfo_get_chip_name(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 13) ?
			socinfo->chip_name : "N/A")
		: "N/A";
}

/* Version 14 */
static uint32_t socinfo_get_num_clusters(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 14) ?
			le32_to_cpu(socinfo->num_clusters) : 0)
		: 0;
}

static uint32_t socinfo_get_ncluster_array_offset(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 14) ?
			le32_to_cpu(socinfo->ncluster_array_offset) : 0)
		: 0;
}

static uint32_t socinfo_get_num_subset_parts(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 14) ?
			le32_to_cpu(socinfo->num_subset_parts) : 0)
		: 0;
}

static uint32_t socinfo_get_nsubset_parts_array_offset(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 14) ?
			le32_to_cpu(socinfo->nsubset_parts_array_offset) : 0)
		: 0;
}

/* Version 15 */
static uint32_t socinfo_get_nmodem_supported(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 15) ?
			le32_to_cpu(socinfo->nmodem_supported) : 0)
		: 0;
}

/* Version 16 */
static uint32_t socinfo_get_eskuid(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 16) ?
			le32_to_cpu(socinfo->esku) : 0)
		: 0;
}

static const char *socinfo_get_esku_mapping(void)
{
	uint32_t id = socinfo_get_eskuid();

	if (id > SKU_UNKNOWN && id < SKU_EXT_RESERVE)
		return hw_platform_esku[id];
	else if (id >= SKU_Y0 && id < SKU_INT_RESERVE)
		return hw_platform_isku[id & SKU_INT_MASK];

	return NULL;
}

static uint32_t socinfo_get_nproduct_code(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 16) ?
			le32_to_cpu(socinfo->nproduct_code) : 0)
		: 0;
}

/* Version 2 */
static ssize_t
msm_get_raw_id(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_raw_id());
}
ATTR_DEFINE(raw_id);

static ssize_t
msm_get_raw_version(struct device *dev,
		     struct device_attribute *attr,
		     char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_raw_version());
}
ATTR_DEFINE(raw_version);

/* Version 3 */
static ssize_t
msm_get_hw_platform(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	uint32_t hw_type;

	hw_type = socinfo_get_platform_type();

	return snprintf(buf, PAGE_SIZE, "%-.32s\n",
			hw_platform[hw_type]);
}
ATTR_DEFINE(hw_platform);

/* Version 4 */
static ssize_t
msm_get_platform_version(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_platform_version());
}
ATTR_DEFINE(platform_version);

/* Version 5 */
static ssize_t
msm_get_accessory_chip(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_accessory_chip());
}
ATTR_DEFINE(accessory_chip);

/* Version 6 */
static ssize_t
msm_get_platform_subtype_id(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	uint32_t hw_subtype;

	hw_subtype = socinfo_get_platform_subtype();
	return snprintf(buf, PAGE_SIZE, "%u\n",
		hw_subtype);
}
ATTR_DEFINE(platform_subtype_id);

static ssize_t
msm_get_platform_subtype(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	uint32_t hw_subtype;

	hw_subtype = socinfo_get_platform_subtype();
	if (socinfo_get_platform_type() == HW_PLATFORM_QRD) {
		if (hw_subtype >= PLATFORM_SUBTYPE_QRD_INVALID) {
			pr_err("Invalid hardware platform sub type for qrd found\n");
			hw_subtype = PLATFORM_SUBTYPE_QRD_INVALID;
		}
		return snprintf(buf, PAGE_SIZE, "%-.32s\n",
					qrd_hw_platform_subtype[hw_subtype]);
	} else {
		if (hw_subtype >= PLATFORM_SUBTYPE_INVALID) {
			pr_err("Invalid hardware platform subtype\n");
			hw_subtype = PLATFORM_SUBTYPE_INVALID;
		}
		return snprintf(buf, PAGE_SIZE, "%-.32s\n",
			hw_platform_subtype[hw_subtype]);
	}
}
ATTR_DEFINE(platform_subtype);

/* Version 7 */
static ssize_t
msm_get_pmic_model(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_pmic_model());
}
ATTR_DEFINE(pmic_model);

static ssize_t
msm_get_pmic_die_revision(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
			 socinfo_get_pmic_die_revision());
}
ATTR_DEFINE(pmic_die_revision);

/* Version 8 (skip) */
/* Version 9 */
static ssize_t
msm_get_foundry_id(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_foundry_id());
}
ATTR_DEFINE(foundry_id);

/* Version 10 */
static ssize_t
msm_get_serial_number(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	uint64_t serial_num_h = socinfo_get_nproduct_id();
	return snprintf(buf, PAGE_SIZE, "0x%016llx\n",
		serial_num_h*0x100000000ULL + socinfo_get_serial_number());
}
ATTR_DEFINE(serial_number);

/* Version 11 (skip) */
/* Version 12 */
static ssize_t
msm_get_chip_family(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_chip_family());
}
ATTR_DEFINE(chip_family);

static ssize_t
msm_get_raw_device_family(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_raw_device_family());
}
ATTR_DEFINE(raw_device_family);

static ssize_t
msm_get_raw_device_number(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_raw_device_number());
}
ATTR_DEFINE(raw_device_number);

/* Version 13 */
static ssize_t
msm_get_nproduct_id(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_nproduct_id());
}
ATTR_DEFINE(nproduct_id);

static ssize_t
msm_get_chip_name(struct device *dev,
		   struct device_attribute *attr,
		   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%-.32s\n",
			socinfo_get_chip_name());
}
ATTR_DEFINE(chip_name);

/* Version 14 */
static ssize_t
msm_get_num_clusters(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_num_clusters());
}
ATTR_DEFINE(num_clusters);

static ssize_t
msm_get_ncluster_array_offset(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_ncluster_array_offset());
}
ATTR_DEFINE(ncluster_array_offset);

uint32_t
socinfo_get_cluster_info(enum subset_cluster_type cluster)
{
	uint32_t sub_cluster, num_cluster, offset;
	void *cluster_val;
	void *info = socinfo;

	if (cluster >= NUM_CLUSTERS_MAX) {
		pr_err("Bad cluster\n");
	return -EINVAL;
	}

	num_cluster = socinfo_get_num_clusters();
	offset = socinfo_get_ncluster_array_offset();

	if (!num_cluster || !offset)
		return -EINVAL;

	info += offset;
	cluster_val = info + (sizeof(uint32_t) * cluster);
	sub_cluster = get_unaligned_le32(cluster_val);

	return sub_cluster;
}
EXPORT_SYMBOL(socinfo_get_cluster_info);

static ssize_t
msm_get_subset_cores(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	uint32_t sub_cluster = socinfo_get_cluster_info(CLUSTER_CPUSS);

	return scnprintf(buf, PAGE_SIZE, "%x\n", sub_cluster);
}
ATTR_DEFINE(subset_cores);

static ssize_t
msm_get_num_subset_parts(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_num_subset_parts());
}
ATTR_DEFINE(num_subset_parts);

static ssize_t
msm_get_nsubset_parts_array_offset(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_nsubset_parts_array_offset());
}
ATTR_DEFINE(nsubset_parts_array_offset);

static uint32_t
socinfo_get_subset_parts(void)
{
	uint32_t num_parts = socinfo_get_num_subset_parts();
	uint32_t offset = socinfo_get_nsubset_parts_array_offset();
	uint32_t sub_parts = 0;
	void *info = socinfo;
	uint32_t part_entry;
	int i;

	if (!num_parts || !offset)
		return -EINVAL;

	info += offset;
	for (i = 0; i < num_parts; i++) {
		part_entry = get_unaligned_le32(info);
		if (part_entry)
			sub_parts |= BIT(i);
		info += sizeof(uint32_t);
	}
	return sub_parts;
}

bool
socinfo_get_part_info(enum subset_part_type part)
{
	uint32_t partinfo;

	if (part >= NUM_PARTS_MAX) {
		pr_err("Bad part number\n");
		return false;
	}

	partinfo = socinfo_get_subset_parts();
	if (partinfo < 0) {
		pr_err("Failed to get part information\n");
		return false;
	}

	return (partinfo & BIT(part));
}
EXPORT_SYMBOL(socinfo_get_part_info);

static ssize_t
msm_get_subset_parts(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	uint32_t sub_parts = socinfo_get_subset_parts();

	return scnprintf(buf, PAGE_SIZE, "%x\n", sub_parts);
}
ATTR_DEFINE(subset_parts);

/* Version 15 */
static ssize_t
msm_get_nmodem_supported(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_nmodem_supported());
}
ATTR_DEFINE(nmodem_supported);

/* Version 16 */
static ssize_t
msm_get_sku(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return sysfs_emit(buf, "%s\n", sku ? sku : "Unknown");
}
ATTR_DEFINE(sku);

static ssize_t
msm_get_esku(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	const char *esku = socinfo_get_esku_mapping();

	return sysfs_emit(buf, "%s\n", esku ? esku : "Unknown");
}
ATTR_DEFINE(esku);
	__le32 nmodem_supported;
};

#ifdef CONFIG_DEBUG_FS
struct socinfo_params {
	u32 raw_device_family;
	u32 hw_plat_subtype;
	u32 accessory_chip;
	u32 raw_device_num;
	u32 chip_family;
	u32 foundry_id;
	u32 plat_ver;
	u32 raw_ver;
	u32 hw_plat;
	u32 fmt;
	u32 nproduct_id;
	u32 num_clusters;
	u32 ncluster_array_offset;
	u32 num_defective_parts;
	u32 ndefective_parts_array_offset;
	u32 nmodem_supported;
};

struct smem_image_version {
	char name[SMEM_IMAGE_VERSION_NAME_SIZE];
	char variant[SMEM_IMAGE_VERSION_VARIANT_SIZE];
	char pad;
	char oem[SMEM_IMAGE_VERSION_OEM_SIZE];
};
#endif /* CONFIG_DEBUG_FS */

struct qcom_socinfo {
	struct soc_device *soc_dev;
	struct soc_device_attribute attr;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dbg_root;
	struct socinfo_params info;
#endif /* CONFIG_DEBUG_FS */
};

struct soc_id {
	unsigned int id;
	const char *name;
};

static const struct soc_id soc_id[] = {
	{ 87, "MSM8960" },
	{ 109, "APQ8064" },
	{ 122, "MSM8660A" },
	{ 123, "MSM8260A" },
	{ 124, "APQ8060A" },
	{ 126, "MSM8974" },
	{ 130, "MPQ8064" },
	{ 138, "MSM8960AB" },
	{ 139, "APQ8060AB" },
	{ 140, "MSM8260AB" },
	{ 141, "MSM8660AB" },
	{ 178, "APQ8084" },
	{ 184, "APQ8074" },
	{ 185, "MSM8274" },
	{ 186, "MSM8674" },
	{ 194, "MSM8974PRO" },
	{ 206, "MSM8916" },
	{ 207, "MSM8994" },
	{ 208, "APQ8074-AA" },
	{ 209, "APQ8074-AB" },
	{ 210, "APQ8074PRO" },
	{ 211, "MSM8274-AA" },
	{ 212, "MSM8274-AB" },
	{ 213, "MSM8274PRO" },
	{ 214, "MSM8674-AA" },
	{ 215, "MSM8674-AB" },
	{ 216, "MSM8674PRO" },
	{ 217, "MSM8974-AA" },
	{ 218, "MSM8974-AB" },
	{ 233, "MSM8936" },
	{ 239, "MSM8939" },
	{ 240, "APQ8036" },
	{ 241, "APQ8039" },
	{ 246, "MSM8996" },
	{ 247, "APQ8016" },
	{ 248, "MSM8216" },
	{ 249, "MSM8116" },
	{ 250, "MSM8616" },
	{ 251, "MSM8992" },
	{ 253, "APQ8094" },
	{ 291, "APQ8096" },
	{ 305, "MSM8996SG" },
	{ 310, "MSM8996AU" },
	{ 311, "APQ8096AU" },
	{ 312, "APQ8096SG" },
	{ 318, "SDM630" },
	{ 321, "SDM845" },
	{ 341, "SDA845" },
	{ 356, "SM8250" },
	{ 402, "IPQ6018" },
	{ 425, "SC7180" },
};

static const char *socinfo_machine(struct device *dev, unsigned int id)
{
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(soc_id); idx++) {
		if (soc_id[idx].id == id)
			return soc_id[idx].name;
	}

	return NULL;
}

#ifdef CONFIG_DEBUG_FS

#define QCOM_OPEN(name, _func)						\
static int qcom_open_##name(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, _func, inode->i_private);		\
}									\
									\
static const struct file_operations qcom_ ##name## _ops = {		\
	.open = qcom_open_##name,					\
	.read = seq_read,						\
	.llseek = seq_lseek,						\
	.release = single_release,					\
}

#define DEBUGFS_ADD(info, name)						\
	debugfs_create_file(__stringify(name), 0400,			\
			    qcom_socinfo->dbg_root,			\
			    info, &qcom_ ##name## _ops)


static int qcom_show_build_id(struct seq_file *seq, void *p)
{
	struct socinfo *socinfo = seq->private;

	seq_printf(seq, "%s\n", socinfo->build_id);

	return 0;
}

static int qcom_show_pmic_model(struct seq_file *seq, void *p)
{
	struct socinfo *socinfo = seq->private;
	int model = SOCINFO_MINOR(le32_to_cpu(socinfo->pmic_model));

	if (model < 0)
		return -EINVAL;

	if (model < ARRAY_SIZE(pmic_models) && pmic_models[model])
		seq_printf(seq, "%s\n", pmic_models[model]);
	else
		seq_printf(seq, "unknown (%d)\n", model);

	return 0;
}

static int qcom_show_pmic_die_revision(struct seq_file *seq, void *p)
{
	struct socinfo *socinfo = seq->private;

	seq_printf(seq, "%u.%u\n",
		   SOCINFO_MAJOR(le32_to_cpu(socinfo->pmic_die_rev)),
		   SOCINFO_MINOR(le32_to_cpu(socinfo->pmic_die_rev)));

	return 0;
}

static ssize_t
msm_set_image_crm_version(struct device *dev,
			struct device_attribute *attr,
			const char *buf,
			size_t count)
{
	char *store_address;

	down_read(&qsocinfo->current_image_rwsem);
	if (qsocinfo->current_image != SMEM_IMAGE_VERSION_PARTITION_APPS) {
		up_read(&qsocinfo->current_image_rwsem);
		return count;
	}
	store_address = socinfo_get_image_version_base_address();
	if (IS_ERR_OR_NULL(store_address)) {
		pr_err("Failed to get image version base address\n");
		up_read(&qsocinfo->current_image_rwsem);
		return count;
	}
	store_address +=
		qsocinfo->current_image * SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE;
	up_read(&qsocinfo->current_image_rwsem);
	store_address += SMEM_IMAGE_VERSION_OEM_OFFSET;
	snprintf(store_address, SMEM_IMAGE_VERSION_OEM_SIZE, "%-.33s", buf);
	return count;
}

static ssize_t
msm_get_image_number(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	int ret;

	down_read(&qsocinfo->current_image_rwsem);
	ret = snprintf(buf, PAGE_SIZE, "%d\n",
			qsocinfo->current_image);
	up_read(&qsocinfo->current_image_rwsem);
	return ret;

}

static ssize_t
msm_select_image(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	int ret, digit;

	ret = kstrtoint(buf, 10, &digit);
	if (ret)
		return ret;
	down_write(&qsocinfo->current_image_rwsem);
	if (digit >= 0 && digit < SMEM_IMAGE_VERSION_BLOCKS_COUNT)
		qsocinfo->current_image = digit;
	else
		qsocinfo->current_image = 0;
	up_write(&qsocinfo->current_image_rwsem);
	return count;
}

static ssize_t
msm_get_images(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int pos = 0;
	int image;
	char *image_address;

	image_address = socinfo_get_image_version_base_address();
	if (IS_ERR_OR_NULL(image_address))
		return snprintf(buf, PAGE_SIZE, "Unavailable\n");

	*buf = '\0';
	for (image = 0; image < SMEM_IMAGE_VERSION_BLOCKS_COUNT; image++) {
		if (*image_address == '\0') {
			image_address += SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE;
			continue;
		}

		pos += snprintf(buf + pos, PAGE_SIZE - pos, "%d:\n",
			image);
		pos += snprintf(buf + pos, PAGE_SIZE - pos,
			"\tCRM:\t\t%-.75s\n", image_address);
		pos += snprintf(buf + pos, PAGE_SIZE - pos,
			"\tVariant:\t%-.20s\n",
			image_address + SMEM_IMAGE_VERSION_VARIANT_OFFSET);
		pos += snprintf(buf + pos, PAGE_SIZE - pos,
			"\tVersion:\t%-.33s\n",
			image_address + SMEM_IMAGE_VERSION_OEM_OFFSET);

		image_address += SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE;
	}

	return pos;
}

static ssize_t
msm_get_machine_name(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	int ret;

	ret = snprintf(buf, SMEM_IMAGE_MACHINE_NAME_SIZE, "%s\n", machine_name_buf);
	return ret;

}

static struct device_attribute image_version =
	__ATTR(image_version, 0644,
			msm_get_image_version, msm_set_image_version);

static struct device_attribute image_variant =
	__ATTR(image_variant, 0644,
			msm_get_image_variant, msm_set_image_variant);

static struct device_attribute image_crm_version =
	__ATTR(image_crm_version, 0644,
			msm_get_image_crm_version, msm_set_image_crm_version);

static struct device_attribute select_image =
	__ATTR(select_image, 0644,
			msm_get_image_number, msm_select_image);

static struct device_attribute images =
	__ATTR(images, 0444, msm_get_images, NULL);

static struct device_attribute machine_name =
	__ATTR(machine_name, 0444, msm_get_machine_name, NULL);


static umode_t soc_info_attribute(struct kobject *kobj,
						   struct attribute *attr,
						   int index)
{
	return attr->mode;
}

static const struct attribute_group custom_soc_attr_group = {
	.attrs = msm_custom_socinfo_attrs,
	.is_visible = soc_info_attribute,
};

static void socinfo_populate_sysfs(struct qcom_socinfo *qcom_socinfo)
{
	int i = 0;

	switch (qcom_socinfo->info.fmt) {
	case SOCINFO_VERSION(0, 15):
		qcom_socinfo->info.nmodem_supported = __le32_to_cpu(info->nmodem_supported);

		debugfs_create_u32("nmodem_supported", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.nmodem_supported);
		fallthrough;
	case SOCINFO_VERSION(0, 14):
		qcom_socinfo->info.num_clusters = __le32_to_cpu(info->num_clusters);
		qcom_socinfo->info.ncluster_array_offset = __le32_to_cpu(info->ncluster_array_offset);
		qcom_socinfo->info.num_defective_parts = __le32_to_cpu(info->num_defective_parts);
		qcom_socinfo->info.ndefective_parts_array_offset = __le32_to_cpu(info->ndefective_parts_array_offset);

		debugfs_create_u32("num_clusters", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.num_clusters);
		debugfs_create_u32("ncluster_array_offset", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.ncluster_array_offset);
		debugfs_create_u32("num_defective_parts", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.num_defective_parts);
		debugfs_create_u32("ndefective_parts_array_offset", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.ndefective_parts_array_offset);
		fallthrough;
	case SOCINFO_VERSION(0, 13):
		qcom_socinfo->info.nproduct_id = __le32_to_cpu(info->nproduct_id);

		debugfs_create_u32("nproduct_id", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.nproduct_id);
		DEBUGFS_ADD(info, chip_id);
		fallthrough;
	case SOCINFO_VERSION(0, 12):
		qcom_socinfo->info.chip_family =
			__le32_to_cpu(info->chip_family);
		qcom_socinfo->info.raw_device_family =
			__le32_to_cpu(info->raw_device_family);
		qcom_socinfo->info.raw_device_num =
			__le32_to_cpu(info->raw_device_num);

		debugfs_create_x32("chip_family", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.chip_family);
		debugfs_create_x32("raw_device_family", 0400,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.raw_device_family);
		debugfs_create_x32("raw_device_number", 0400,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.raw_device_num);
		fallthrough;
	case SOCINFO_VERSION(0, 11):
	case SOCINFO_VERSION(0, 10):
	case SOCINFO_VERSION(0, 9):
		qcom_socinfo->info.foundry_id = __le32_to_cpu(info->foundry_id);

		debugfs_create_u32("foundry_id", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.foundry_id);
		fallthrough;
	case SOCINFO_VERSION(0, 8):
	case SOCINFO_VERSION(0, 7):
		DEBUGFS_ADD(info, pmic_model);
		DEBUGFS_ADD(info, pmic_die_rev);
		fallthrough;
	case SOCINFO_VERSION(0, 6):
		qcom_socinfo->info.hw_plat_subtype =
			__le32_to_cpu(info->hw_plat_subtype);

		debugfs_create_u32("hardware_platform_subtype", 0400,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.hw_plat_subtype);
		fallthrough;
	case SOCINFO_VERSION(0, 5):
		qcom_socinfo->info.accessory_chip =
			__le32_to_cpu(info->accessory_chip);

		debugfs_create_u32("accessory_chip", 0400,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.accessory_chip);
		fallthrough;
	case SOCINFO_VERSION(0, 4):
		qcom_socinfo->info.plat_ver = __le32_to_cpu(info->plat_ver);

		debugfs_create_u32("platform_version", 0400,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.plat_ver);
		fallthrough;
	case SOCINFO_VERSION(0, 3):
		qcom_socinfo->info.hw_plat = __le32_to_cpu(info->hw_plat);

		debugfs_create_u32("hardware_platform", 0400,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.hw_plat);
		fallthrough;
	case SOCINFO_VERSION(0, 2):
		qcom_socinfo->info.raw_ver  = __le32_to_cpu(info->raw_ver);

		debugfs_create_u32("raw_version", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.raw_ver);
		fallthrough;
	case SOCINFO_VERSION(0, 1):
		DEBUGFS_ADD(info, build_id);
		break;
	}

	msm_custom_socinfo_attrs[i++] = &image_version.attr;
	msm_custom_socinfo_attrs[i++] = &image_variant.attr;
	msm_custom_socinfo_attrs[i++] = &image_crm_version.attr;
	msm_custom_socinfo_attrs[i++] = &select_image.attr;
	msm_custom_socinfo_attrs[i++] = &images.attr;
	msm_custom_socinfo_attrs[i++] = &machine_name.attr;
	msm_custom_socinfo_attrs[i++] = NULL;
	qcom_socinfo->attr.custom_attr_group = &custom_soc_attr_group;
}

static void socinfo_print(void)
{
	uint32_t f_maj = SOCINFO_MAJOR(socinfo_format);
	uint32_t f_min = SOCINFO_MINOR(socinfo_format);
	uint32_t v_maj = SOCINFO_MAJOR(le32_to_cpu(socinfo->ver));
	uint32_t v_min = SOCINFO_MINOR(le32_to_cpu(socinfo->ver));

	switch (socinfo_format) {
	case SOCINFO_VERSION(0, 1):
		pr_info("v%u.%u, id=%u, ver=%u.%u\n",
				f_maj, f_min, socinfo->id, v_maj, v_min);
		break;
	case SOCINFO_VERSION(0, 2):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u\n",
				f_maj, f_min, socinfo->id, v_maj, v_min,
				socinfo->raw_id, socinfo->raw_ver);
		break;
	case SOCINFO_VERSION(0, 3):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u\n",
				f_maj, f_min, socinfo->id, v_maj, v_min,
				socinfo->raw_id, socinfo->raw_ver,
				socinfo->hw_plat);
		break;
	case SOCINFO_VERSION(0, 4):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u, hw_plat_ver=%u\n",
			f_maj, f_min, socinfo->id, v_maj, v_min,
			socinfo->raw_id, socinfo->raw_ver,
			socinfo->hw_plat,
			socinfo->plat_ver);
		break;
	case SOCINFO_VERSION(0, 5):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u, hw_plat_ver=%u\n accessory_chip=%u\n",
			f_maj, f_min, socinfo->id, v_maj, v_min,
			socinfo->raw_id, socinfo->raw_ver,
			socinfo->hw_plat,
			socinfo->plat_ver,
			socinfo->accessory_chip);
		break;
	case SOCINFO_VERSION(0, 6):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u, hw_plat_ver=%u\n accessory_chip=%u hw_plat_subtype=%u\n",
			f_maj, f_min, socinfo->id, v_maj, v_min,
			socinfo->raw_id, socinfo->raw_ver,
			socinfo->hw_plat,
			socinfo->plat_ver,
			socinfo->accessory_chip,
			socinfo->hw_plat_subtype);
		break;
	case SOCINFO_VERSION(0, 7):
	case SOCINFO_VERSION(0, 8):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u, hw_plat_ver=%u\n accessory_chip=%u, hw_plat_subtype=%u, pmic_model=%u, pmic_die_revision=%u\n",
			f_maj, f_min, socinfo->id, v_maj, v_min,
			socinfo->raw_id, socinfo->raw_ver,
			socinfo->hw_plat,
			socinfo->plat_ver,
			socinfo->accessory_chip,
			socinfo->hw_plat_subtype,
			socinfo->pmic_model,
			socinfo->pmic_die_rev);
		break;
	case SOCINFO_VERSION(0, 9):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u, hw_plat_ver=%u\n accessory_chip=%u, hw_plat_subtype=%u, pmic_model=%u, pmic_die_revision=%u foundry_id=%u\n",
			f_maj, f_min, socinfo->id, v_maj, v_min,
			socinfo->raw_id, socinfo->raw_ver,
			socinfo->hw_plat,
			socinfo->plat_ver,
			socinfo->accessory_chip,
			socinfo->hw_plat_subtype,
			socinfo->pmic_model,
			socinfo->pmic_die_rev,
			socinfo->foundry_id);
		break;
	case SOCINFO_VERSION(0, 10):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u, hw_plat_ver=%u\n accessory_chip=%u, hw_plat_subtype=%u, pmic_model=%u, pmic_die_revision=%u foundry_id=%u serial_number=%u\n",
			f_maj, f_min, socinfo->id, v_maj, v_min,
			socinfo->raw_id, socinfo->raw_ver,
			socinfo->hw_plat,
			socinfo->plat_ver,
			socinfo->accessory_chip,
			socinfo->hw_plat_subtype,
			socinfo->pmic_model,
			socinfo->pmic_die_rev,
			socinfo->foundry_id,
			socinfo->serial_num);
		break;
	case SOCINFO_VERSION(0, 11):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u, hw_plat_ver=%u\n accessory_chip=%u, hw_plat_subtype=%u, pmic_model=%u, pmic_die_revision=%u foundry_id=%u serial_number=%u num_pmics=%u\n",
			f_maj, f_min, socinfo->id, v_maj, v_min,
			socinfo->raw_id, socinfo->raw_ver,
			socinfo->hw_plat,
			socinfo->plat_ver,
			socinfo->accessory_chip,
			socinfo->hw_plat_subtype,
			socinfo->pmic_model,
			socinfo->pmic_die_rev,
			socinfo->foundry_id,
			socinfo->serial_num,
			socinfo->num_pmics);
		break;
	case SOCINFO_VERSION(0, 12):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u, hw_plat_ver=%u\n accessory_chip=%u, hw_plat_subtype=%u, pmic_model=%u, pmic_die_revision=%u foundry_id=%u serial_number=%u num_pmics=%u chip_family=0x%x raw_device_family=0x%x raw_device_number=0x%x\n",
			f_maj, f_min, socinfo->id, v_maj, v_min,
			socinfo->raw_id, socinfo->raw_ver,
			socinfo->hw_plat,
			socinfo->plat_ver,
			socinfo->accessory_chip,
			socinfo->hw_plat_subtype,
			socinfo->pmic_model,
			socinfo->pmic_die_rev,
			socinfo->foundry_id,
			socinfo->serial_num,
			socinfo->num_pmics,
			socinfo->chip_family,
			socinfo->raw_device_family,
			socinfo->raw_device_num);
		break;
	case SOCINFO_VERSION(0, 13):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u, hw_plat_ver=%u\n accessory_chip=%u, hw_plat_subtype=%u, pmic_model=%u, pmic_die_revision=%u foundry_id=%u serial_number=%u num_pmics=%u chip_family=0x%x raw_device_family=0x%x raw_device_number=0x%x nproduct_id=0x%x\n",
			f_maj, f_min, socinfo->id, v_maj, v_min,
			socinfo->raw_id, socinfo->raw_ver,
			socinfo->hw_plat,
			socinfo->plat_ver,
			socinfo->accessory_chip,
			socinfo->hw_plat_subtype,
			socinfo->pmic_model,
			socinfo->pmic_die_rev,
			socinfo->foundry_id,
			socinfo->serial_num,
			socinfo->num_pmics,
			socinfo->chip_family,
			socinfo->raw_device_family,
			socinfo->raw_device_num,
			socinfo->nproduct_id);
		break;

	case SOCINFO_VERSION(0, 14):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u, hw_plat_ver=%u\n accessory_chip=%u, hw_plat_subtype=%u, pmic_model=%u, pmic_die_revision=%u foundry_id=%u serial_number=%u num_pmics=%u chip_family=0x%x raw_device_family=0x%x raw_device_number=0x%x nproduct_id=0x%x num_clusters=0x%x ncluster_array_offset=0x%x num_subset_parts=0x%x nsubset_parts_array_offset=0x%x\n",
			f_maj, f_min, socinfo->id, v_maj, v_min,
			socinfo->raw_id, socinfo->raw_ver,
			socinfo->hw_plat,
			socinfo->plat_ver,
			socinfo->accessory_chip,
			socinfo->hw_plat_subtype,
			socinfo->pmic_model,
			socinfo->pmic_die_rev,
			socinfo->foundry_id,
			socinfo->serial_num,
			socinfo->num_pmics,
			socinfo->chip_family,
			socinfo->raw_device_family,
			socinfo->raw_device_num,
			socinfo->nproduct_id,
			socinfo->num_clusters,
			socinfo->ncluster_array_offset,
			socinfo->num_subset_parts,
			socinfo->nsubset_parts_array_offset);
		break;

	case SOCINFO_VERSION(0, 15):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u, hw_plat_ver=%u\n accessory_chip=%u, hw_plat_subtype=%u, pmic_model=%u, pmic_die_revision=%u foundry_id=%u serial_number=%u num_pmics=%u chip_family=0x%x raw_device_family=0x%x raw_device_number=0x%x nproduct_id=0x%x num_clusters=0x%x ncluster_array_offset=0x%x num_subset_parts=0x%x nsubset_parts_array_offset=0x%x nmodem_supported=0x%x\n",
			f_maj, f_min, socinfo->id, v_maj, v_min,
			socinfo->raw_id, socinfo->raw_ver,
			socinfo->hw_plat,
			socinfo->plat_ver,
			socinfo->accessory_chip,
			socinfo->hw_plat_subtype,
			socinfo->pmic_model,
			socinfo->pmic_die_rev,
			socinfo->foundry_id,
			socinfo->serial_num,
			socinfo->num_pmics,
			socinfo->chip_family,
			socinfo->raw_device_family,
			socinfo->raw_device_num,
			socinfo->nproduct_id,
			socinfo->num_clusters,
			socinfo->ncluster_array_offset,
			socinfo->num_subset_parts,
			socinfo->nsubset_parts_array_offset,
			socinfo->nmodem_supported);
		break;

	case SOCINFO_VERSION(0, 16):
		pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, raw_ver=%u, hw_plat=%u, hw_plat_ver=%u\n accessory_chip=%u, hw_plat_subtype=%u, pmic_model=%u, pmic_die_revision=%u foundry_id=%u serial_number=%u num_pmics=%u chip_family=0x%x raw_device_family=0x%x raw_device_number=0x%x nproduct_id=0x%x num_clusters=0x%x ncluster_array_offset=0x%x num_subset_parts=0x%x nsubset_parts_array_offset=0x%x nmodem_supported=0x%x sku=%s\n",
			f_maj, f_min, socinfo->id, v_maj, v_min,
			socinfo->raw_id, socinfo->raw_ver,
			socinfo->hw_plat,
			socinfo->plat_ver,
			socinfo->accessory_chip,
			socinfo->hw_plat_subtype,
			socinfo->pmic_model,
			socinfo->pmic_die_rev,
			socinfo->foundry_id,
			socinfo->serial_num,
			socinfo->num_pmics,
			socinfo->chip_family,
			socinfo->raw_device_family,
			socinfo->raw_device_num,
			socinfo->nproduct_id,
			socinfo->num_clusters,
			socinfo->ncluster_array_offset,
			socinfo->num_subset_parts,
			socinfo->nsubset_parts_array_offset,
			socinfo->nmodem_supported,
			sku ? sku : "Unknown");
		break;

	default:
		pr_err("Unknown format found: v%u.%u\n", f_maj, f_min);
		break;
	}
}

static const char *socinfo_machine(unsigned int id)
{
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(soc_id); idx++) {
		if (soc_id[idx].id == id)
			return soc_id[idx].name;
	}

	return NULL;
}

uint32_t socinfo_get_id(void)
{
	return (socinfo) ? le32_to_cpu(socinfo->id) : 0;
}
EXPORT_SYMBOL(socinfo_get_id);

const char *socinfo_get_id_string(void)
{
	uint32_t id = socinfo_get_id();

	return socinfo_machine(id);
}
EXPORT_SYMBOL(socinfo_get_id_string);

static const char *of_parse_machine_name(void)
{
	struct device_node *root;
	const char *machine_name = NULL;

	root = of_find_node_by_path("/");
	if (!root)
		return NULL;
	of_property_read_string(root, "model", &machine_name);
	if (!machine_name)
		of_property_read_string(root, "compatible", &machine_name);

	return machine_name;
}

static int qcom_socinfo_probe(struct platform_device *pdev)
{
	struct qcom_socinfo *qs;
	struct socinfo *info;
	size_t item_size;
	const char *machine, *esku;

	machine_name_buf = of_parse_machine_name();
	if (IS_ERR(machine_name_buf)) {
		dev_err(&pdev->dev, "Couldn't find machine name\n");
		return PTR_ERR(machine_name_buf);
	}

	info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_HW_SW_BUILD_ID,
			      &item_size);
	if (IS_ERR(info)) {
		dev_err(&pdev->dev, "Couldn't find socinfo\n");
		return PTR_ERR(info);
	}

	qs = devm_kzalloc(&pdev->dev, sizeof(*qs), GFP_KERNEL);
	if (!qs)
		return -ENOMEM;

	qs->attr.family = "Snapdragon";
	qs->attr.machine = socinfo_machine(&pdev->dev,
					   le32_to_cpu(info->id));
	qs->attr.soc_id = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%u",
					 le32_to_cpu(info->id));
	qs->attr.revision = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%u.%u",
					   SOCINFO_MAJOR(le32_to_cpu(info->ver)),
					   SOCINFO_MINOR(le32_to_cpu(info->ver)));
	if (offsetof(struct socinfo, serial_num) <= item_size)
		qs->attr.serial_number = devm_kasprintf(&pdev->dev, GFP_KERNEL,
							"%u",
							le32_to_cpu(info->serial_num));

	qs->soc_dev = soc_device_register(&qs->attr);
	if (IS_ERR(qs->soc_dev))
		return PTR_ERR(qs->soc_dev);

	socinfo_debugfs_init(qs, info);

	/* Feed the soc specific unique data into entropy pool */
	add_device_randomness(info, item_size);

	platform_set_drvdata(pdev, qs);

	return 0;
}

static int qcom_socinfo_remove(struct platform_device *pdev)
{
	struct qcom_socinfo *qs = platform_get_drvdata(pdev);

	soc_device_unregister(qs->soc_dev);

	socinfo_debugfs_exit(qs);

	return 0;
}

static struct platform_driver qcom_socinfo_driver = {
	.probe = qcom_socinfo_probe,
	.remove = qcom_socinfo_remove,
	.driver  = {
		.name = "qcom-socinfo",
	},
};

module_platform_driver(qcom_socinfo_driver);

MODULE_DESCRIPTION("Qualcomm SoCinfo driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qcom-socinfo");
