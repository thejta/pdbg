#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <ccan/list/list.h>
#include <libfdt/libfdt.h>

#include "bitutils.h"
#include "target.h"
#include "device.h"
#include "operations.h"

#undef PR_DEBUG
#define PR_DEBUG(...)

struct list_head empty_list = LIST_HEAD_INIT(empty_list);
struct list_head target_classes = LIST_HEAD_INIT(target_classes);

/* Work out the address to access based on the current target and
 * final class name */
static struct dt_node *get_class_target_addr(struct dt_node *dn, const char *name, uint64_t *addr)
{
	/* Check class */
	while (strcmp(dn->target->class_type, name)) {
		/* Keep walking the tree translating addresses */
		*addr += dt_get_address(dn, 0, NULL);
		dn = dn->parent;

		/* The should always be a parent. If there isn't it
		 * means we traversed up the whole device tree and
		 * didn't find a parent matching the given class. */
		assert(dn);
		assert(dn->target);
	}

	return dn;
}

/* The indirect access code was largely stolen from hw/xscom.c in skiboot */
#define PIB_IND_MAX_RETRIES 10
#define PIB_IND_READ PPC_BIT(0)
#define PIB_IND_ADDR PPC_BITMASK(12, 31)
#define PIB_IND_DATA PPC_BITMASK(48, 63)

#define PIB_DATA_IND_COMPLETE PPC_BIT(32)
#define PIB_DATA_IND_ERR PPC_BITMASK(33, 35)
#define PIB_DATA_IND_DATA PPC_BITMASK(48, 63)

static int pib_indirect_read(struct pib *pib, uint64_t addr, uint64_t *data)
{
	uint64_t indirect_addr;
	int retries;

	if ((addr >> 60) & 1) {
		PR_ERROR("Indirect form 1 not supported\n");
		return -1;
	}

	indirect_addr = addr & 0x7fffffff;
	*data = PIB_IND_READ | (addr & PIB_IND_ADDR);
	CHECK_ERR(pib->write(pib, indirect_addr, *data));

	/* Wait for completion */
	for (retries = 0; retries < PIB_IND_MAX_RETRIES; retries++) {
		CHECK_ERR(pib->read(pib, indirect_addr, data));

		if ((*data & PIB_DATA_IND_COMPLETE) &&
		    ((*data & PIB_DATA_IND_ERR) == 0)) {
			*data = *data & PIB_DATA_IND_DATA;
			break;
		}

		if ((*data & PIB_DATA_IND_COMPLETE) ||
		    (retries >= PIB_IND_MAX_RETRIES)) {
			PR_ERROR("Error reading indirect register");
			return -1;
		}
	}

	return 0;
}

static int pib_indirect_write(struct pib *pib, uint64_t addr, uint64_t data)
{
	uint64_t indirect_addr;
	int retries;

	if ((addr >> 60) & 1) {
		PR_ERROR("Indirect form 1 not supported\n");
		return -1;
	}

	indirect_addr = addr & 0x7fffffff;
	data &= PIB_IND_DATA;
	data |= addr & PIB_IND_ADDR;
	CHECK_ERR(pib->write(pib, indirect_addr, data));

	/* Wait for completion */
	for (retries = 0; retries < PIB_IND_MAX_RETRIES; retries++) {
		CHECK_ERR(pib->read(pib, indirect_addr, &data));

		if ((data & PIB_DATA_IND_COMPLETE) &&
		    ((data & PIB_DATA_IND_ERR) == 0))
			break;

		if ((data & PIB_DATA_IND_COMPLETE) ||
		    (retries >= PIB_IND_MAX_RETRIES)) {
			PR_ERROR("Error writing indirect register");
			return -1;
		}
	}

	return 0;
}

int pib_read(struct target *pib_dt, uint64_t addr, uint64_t *data)
{
	struct pib *pib;
	struct dt_node *dn = pib_dt->dn;
	int rc;

	dn = get_class_target_addr(dn, "pib", &addr);
	pib_dt = dn->target;
	pib = target_to_pib(pib_dt);
	if (addr & PPC_BIT(0))
		rc = pib_indirect_read(pib, addr, data);
	else
		rc = pib->read(pib, addr, data);
	return rc;
}

int pib_write(struct target *pib_dt, uint64_t addr, uint64_t data)
{
	struct pib *pib;
	struct dt_node *dn = pib_dt->dn;
	int rc;

	dn = get_class_target_addr(dn, "pib", &addr);
	pib_dt = dn->target;
	pib = target_to_pib(pib_dt);
	if (addr & PPC_BIT(0))
		rc = pib_indirect_write(pib, addr, data);
	else
		rc = pib->write(pib, addr, data);
	return rc;
}

int opb_read(struct target *opb_dt, uint32_t addr, uint32_t *data)
{
	struct opb *opb;
	struct dt_node *dn = opb_dt->dn;
	uint64_t addr64 = addr;

	dn = get_class_target_addr(dn, "opb", &addr64);
	opb_dt = dn->target;
	opb = target_to_opb(opb_dt);
	return opb->read(opb, addr64, data);
}

int opb_write(struct target *opb_dt, uint32_t addr, uint32_t data)
{
	struct opb *opb;
	struct dt_node *dn = opb_dt->dn;
	uint64_t addr64 = addr;

	dn = get_class_target_addr(dn, "opb", &addr64);
	opb_dt = dn->target;
	opb = target_to_opb(opb_dt);

	return opb->write(opb, addr64, data);
}

int fsi_read(struct target *fsi_dt, uint32_t addr, uint32_t *data)
{
	struct fsi *fsi;
	struct dt_node *dn = fsi_dt->dn;
	uint64_t addr64 = addr;

	dn = get_class_target_addr(dn, "fsi", &addr64);
	fsi_dt = dn->target;
	fsi = target_to_fsi(fsi_dt);
	return fsi->read(fsi, addr64, data);
}

int fsi_write(struct target *fsi_dt, uint32_t addr, uint32_t data)
{
	struct fsi *fsi;
	struct dt_node *dn = fsi_dt->dn;
	uint64_t addr64 = addr;

	dn = get_class_target_addr(dn, "fsi", &addr64);
	fsi_dt = dn->target;
	fsi = target_to_fsi(fsi_dt);

	return fsi->write(fsi, addr64, data);
}

struct target *require_target_parent(struct target *target)
{
	struct dt_node *dn;

	assert((dn = target->dn));
	return dn->parent->target;
}

/* Finds the given class. Returns NULL if not found. */
struct target_class *find_target_class(const char *name)
{
	struct target_class *target_class;

	list_for_each(&target_classes, target_class, class_head_link)
		if (!strcmp(target_class->name, name))
			return target_class;

	return NULL;
}

/* Same as above but dies with an assert if the target class doesn't
 * exist */
struct target_class *require_target_class(const char *name)
{
	struct target_class *target_class;

	target_class = find_target_class(name);
	if (!target_class) {
		PR_ERROR("Couldn't find class %s\n", name);
		assert(0);
	}
	return target_class;
}

/* Returns the existing class or allocates space for a new one */
static struct target_class *get_target_class(const char *name)
{
	struct target_class *target_class;

	if ((target_class = find_target_class(name)))
		return target_class;

	/* Need to allocate a new class */
	PR_DEBUG("Allocating %s target class\n", name);
	target_class = calloc(1, sizeof(*target_class));
	assert(target_class);
	target_class->name = strdup(name);
	list_head_init(&target_class->targets);
	list_add(&target_classes, &target_class->class_head_link);
	return target_class;
}

struct hw_unit_info *__start_hw_units;
struct hw_init_info *__stop_hw_units;
struct hw_unit_info *find_compatible_target(const char *compat)
{
	struct hw_unit_info **p;
	struct target *target;

	for (p = &__start_hw_units; p < (struct hw_unit_info **) &__stop_hw_units; p++) {
		target = (*p)->hw_unit + (*p)->struct_target_offset;
		if (!strcmp(target->compatible, compat))
			return *p;
	}

	return NULL;
}

void targets_init(void *fdt)
{
	struct dt_node *dn;
	const struct dt_property *p;
	struct target_class *target_class;
	struct hw_unit_info *hw_unit_info;
	void *new_hw_unit;
	struct target *new_target;
	uint32_t index;

	dt_root = dt_new_root("");
	dt_expand(fdt);

	/* Now we need to walk the device-tree, assign struct targets
	 * to each of the nodes and add them to the appropriate target
	 * classes */
	dt_for_each_node(dt_root, dn) {
		p = dt_require_property(dn, "compatible", -1);
		hw_unit_info = find_compatible_target(p->prop);
		if (hw_unit_info) {
			/* We need to allocate a new target */
			new_hw_unit = malloc(hw_unit_info->size);
			assert(new_hw_unit);
			memcpy(new_hw_unit, hw_unit_info->hw_unit, hw_unit_info->size);
			new_target = new_hw_unit + hw_unit_info->struct_target_offset;
			new_target->dn = dn;
			dn->target = new_target;
			index = dt_prop_get_u32_def(dn, "index", -1);
			dn->target->index = index;
			target_class = get_target_class(new_target->class_type);
			list_add(&target_class->targets, &new_target->class_link);
			PR_DEBUG("Found target %s for %s\n", new_target->name, dn->name);
		} else
			PR_DEBUG("No target found for %s\n", dn->name);
	}
}

/* Disable a node and all it's children */
static void disable_node(struct dt_node *dn)
{
	struct dt_node *next;
	struct dt_property *p;

	p = dt_find_property(dn, "status");
	if (p)
		dt_del_property(dn, p);

	dt_add_property_string(dn, "status", "disabled");
	dt_for_each_child(dn, next)
		disable_node(next);
}

static void _target_probe(struct dt_node *dn)
{
	int rc;
	struct dt_node *next;
	struct dt_property *p;

	PR_DEBUG("Probe %s - ", dn->name);
	if (!dn->target) {
		PR_DEBUG("target not found\n");
		return;
	}

	p = dt_find_property(dn, "status");
	if ((p && !strcmp(p->prop, "disabled")) || (dn->target->probe && (rc = dn->target->probe(dn->target)))) {
		if (rc)
			PR_DEBUG("not found\n");
		else
			PR_DEBUG("disabled\n");

		disable_node(dn);
	} else {
		PR_DEBUG("success\n");
		dt_for_each_child(dn, next)
			_target_probe(next);
	}
}

/* We walk the tree root down disabling targets which might/should
 * exist but don't */
void target_probe(void)
{
	struct dt_node *dn;

	dt_for_each_node(dt_root, dn)
		_target_probe(dn);
}
