#pragma once

namespace ts
{
void	TSCALL hide_hardware_cursor(void);
void	TSCALL show_hardware_cursor(void);

irect   TSCALL wnd_get_max_size_fs(int x, int y);
irect   TSCALL wnd_get_max_size(int x, int y);

irect   TSCALL wnd_get_max_size(const irect &rfrom);
irect   TSCALL wnd_get_max_size_fs(const irect &rfrom);

irect   TSCALL wnd_get_max_size(HWND hwnd);

ivec2   TSCALL wnd_get_center_pos( const ts::ivec2& size );
void    TSCALL wnd_fix_rect(irect &r, int minw, int minh);

int     TSCALL monitor_count(void);
irect   TSCALL monitor_get_max_size_fs(int monitor);
irect   TSCALL monitor_get_max_size(int monitor);

ivec2 TSCALL center_pos_by_window(HWND hwnd);

wstr_c TSCALL get_clipboard_text(void);
void TSCALL set_clipboard_text(const wsptr &text);

bitmap_c TSCALL get_clipboard_bitmap(void);
void TSCALL set_clipboard_bitmap(const bitmap_c &bmp);

bool start_app( const wsptr &cmdline, HANDLE *hProcess );

}
