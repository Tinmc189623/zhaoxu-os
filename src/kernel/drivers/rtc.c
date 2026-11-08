#include <io.h>
#include <rtc.h>
#include <types.h>

static u8 cmos_read(u8 reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static int update_in_progress(void)
{
    return (cmos_read(0x0A) & 0x80) != 0;
}

static u8 bcd_to_bin(u8 value)
{
    return (u8)((value & 0x0F) + ((value >> 4) * 10));
}

static int read_once(struct rtc_time *time)
{
    for (u32 guard = 0; guard < 100000 && update_in_progress(); guard++)
        ;
    if (update_in_progress())
        return -1;

    u8 second = cmos_read(0x00);
    u8 minute = cmos_read(0x02);
    u8 hour = cmos_read(0x04);
    u8 day = cmos_read(0x07);
    u8 month = cmos_read(0x08);
    u8 year = cmos_read(0x09);
    u8 weekday = cmos_read(0x06);
    u8 status_b = cmos_read(0x0B);

    if (!(status_b & 0x04)) {
        second = bcd_to_bin(second);
        minute = bcd_to_bin(minute);
        hour = (u8)((hour & 0x80) | bcd_to_bin(hour & 0x7F));
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
        weekday = bcd_to_bin(weekday);
    }

    if (!(status_b & 0x02) && (hour & 0x80))
        hour = (u8)(((hour & 0x7F) + 12) % 24);

    time->second = second;
    time->minute = minute;
    time->hour = hour & 0x7F;
    time->day = day;
    time->month = month;
    time->year = (u16)(2000 + year);
    time->weekday = weekday;
    return 0;
}

int rtc_read(struct rtc_time *time)
{
    struct rtc_time first, second;
    if (read_once(&first) != 0)
        return -1;
    if (read_once(&second) != 0)
        return -1;
    *time = second;
    (void)first;
    return 0;
}
