// SPDX-License-Identifier: GPL-3.0-or-later

#include "otgy/av_c_api.h"

#include <stddef.h>

int main(void) {
  otgy_error* error = NULL;
  otgy_chart* chart = NULL;
  if (otgy_c_api_version() != OTGY_C_API_VERSION) {
    return 1;
  }
  if (otgy_chart_create_empty(&chart, &error) != OTGY_STATUS_OK ||
      chart == NULL || error != NULL) {
    otgy_error_destroy(error);
    otgy_chart_destroy(chart);
    return 2;
  }
  otgy_chart_destroy(chart);
  otgy_error_destroy(NULL);
  otgy_bytes_destroy(NULL);
  otgy_pcm_destroy(NULL);
  return 0;
}
