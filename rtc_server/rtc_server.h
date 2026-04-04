#ifndef RTC_SERVER_H
#define RTC_SERVER_H

#include <time.h>

void rtc_server_init(void);
void rtc_server_sync_from_host(const char *ip_str);
time_t rtc_server_get_time(void);

#endif
