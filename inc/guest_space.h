#pragma once

struct app_permissions {
	short app_present;
	short app_writeable;
};

struct guest_entry {
	short valid;
	uint64_t app_va;
	struct app_permissions permissions;
};

// base and bound of application heap/stack in guest os va
// replace proc bitmap value with this struct
struct app_va_segment {
	short owned;
	uint64_t bound;
	uint64_t midpoint;
	uint64_t base;
};
