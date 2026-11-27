/* SPDX-License-Identifier: GPL-3.0-only */
/* The R36S's facts for the log pack's hardware report (src/hwreport.h): its
 * panel, its inputs, Panfrost's line about the Mali GPU, and the kernel's
 * lines about the panel, which say which of the R36S's panels this one is
 * driven as (docs/devices/r36s.md, "Panels"). It has no closed files. */
#include "hwfacts.h"

void plat_hw_facts(hw_facts *f)
{
	static char klog[1 << 18];
	char line[160];

	memset(f, 0, sizeof *f);
	f->closed_total = -1;
	snprintf(f->panel, sizeof f->panel, "built-in, 640x480");
	hwf_common(f);
	hwf_klog(klog, sizeof klog);
	if (hw_klog_line(klog, "panel", line, sizeof line) || hw_klog_line(klog, "dsi", line, sizeof line))
		snprintf(f->panel, sizeof f->panel, "built-in, 640x480: %.70s", line);
	if (hw_klog_line(klog, "panfrost", line, sizeof line))
		snprintf(f->gpu_kernel, sizeof f->gpu_kernel, "%s", line);
}
