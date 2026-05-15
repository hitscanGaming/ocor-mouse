/* REQ-DPI-001: enforce step size */
int set_dpi(int dpi) { return dpi % 50 == 0 ? 0 : -1; }
