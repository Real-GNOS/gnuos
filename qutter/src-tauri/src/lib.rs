use std::io::Cursor;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use base64::{engine::general_purpose::STANDARD as BASE64, Engine as _};
use image::{ImageFormat, RgbaImage};
use tauri::{AppHandle, Emitter, Manager};
use tauri_plugin_clipboard_manager::ClipboardExt;
use tauri_plugin_global_shortcut::{Code, Modifiers, Shortcut, ShortcutState};

/// 长截图共享状态：取消标志 + 最近一次长图 PNG 缓存
#[derive(Default)]
struct LongCaptureState {
    cancel: AtomicBool,
    last_long_png: Mutex<Option<Vec<u8>>>,
}

/// 长截图参数
const SCROLL_OVERLAP: f64 = 0.2; // 相邻帧保留 20% 重叠
const MAX_FRAMES: u32 = 120; // 帧数上限
const MAX_HEIGHT: u32 = 24_000; // 长图高度上限（px），防内存爆炸
const FRAME_SETTLE_MS: u64 = 500; // 滚动后等待页面稳定
const STRIP_H: u32 = 32; // 模板匹配条带高度
const PROGRESS_W: u32 = 300; // 截取中进度窗尺寸
const PROGRESS_H: u32 = 110;

/// 前端把选区裁剪成 PNG 字节后调用，写入系统剪贴板
#[tauri::command]
fn copy_image(app: AppHandle, png: Vec<u8>) -> Result<(), String> {
    let img = tauri::image::Image::from_bytes(&png).map_err(|e| e.to_string())?;
    app.clipboard()
        .write_image(&img)
        .map_err(|e| e.to_string())
}

/// 结束一次截图（确认复制或 Esc 取消后），窗口退回待机状态
#[tauri::command]
fn finish_capture(app: AppHandle) -> Result<(), String> {
    let win = app.get_webview_window("main").ok_or("找不到主窗口")?;
    win.set_fullscreen(false).map_err(|e| e.to_string())?;
    win.hide().map_err(|e| e.to_string())?;
    Ok(())
}

/// 退出应用
#[tauri::command]
fn exit_app(app: AppHandle) {
    app.exit(0);
}

/// 截取主显示器全屏图像
fn capture_primary_image() -> Result<RgbaImage, String> {
    let monitors = xcap::Monitor::all().map_err(|e| e.to_string())?;
    let monitor = monitors
        .iter()
        .find(|m| m.is_primary().unwrap_or(false))
        .or_else(|| monitors.first())
        .ok_or("没有可用的显示器")?;
    monitor.capture_image().map_err(|e| e.to_string())
}

/// 截取主显示器全屏，返回 (物理宽, 物理高, base64 PNG)
fn capture_primary_screen() -> Result<(u32, u32, String), String> {
    let img = capture_primary_image()?;
    let (w, h) = (img.width(), img.height());
    let mut buf: Vec<u8> = Vec::new();
    image::DynamicImage::ImageRgba8(img)
        .write_to(&mut Cursor::new(&mut buf), ImageFormat::Png)
        .map_err(|e| e.to_string())?;
    Ok((w, h, BASE64.encode(&buf)))
}

/// 裁剪屏幕上的选区区域（越界自动收敛）
fn capture_region(x: u32, y: u32, w: u32, h: u32) -> Result<RgbaImage, String> {
    let full = capture_primary_image()?;
    let (fw, fh) = (full.width(), full.height());
    let x = x.min(fw.saturating_sub(1));
    let y = y.min(fh.saturating_sub(1));
    let w = w.min(fw - x);
    let h = h.min(fh - y);
    if w == 0 || h == 0 {
        return Err("选区超出屏幕范围".into());
    }
    Ok(image::imageops::crop_imm(&full, x, y, w, h).to_image())
}

/// 全局快捷键按下：先隐藏主窗口（避免 Qutter 自己入镜），截屏后全屏置顶显示
fn start_capture(app: AppHandle) {
    if let Some(win) = app.get_webview_window("main") {
        let _ = win.hide();
    }
    std::thread::sleep(Duration::from_millis(150)); // 等窗口从屏幕消失

    match capture_primary_screen() {
        Ok((w, h, data)) => {
            if let Some(win) = app.get_webview_window("main") {
                let _ = win.set_fullscreen(true);
                let _ = win.show();
                let _ = win.set_focus();
            }
            let _ = app.emit(
                "screenshot-ready",
                serde_json::json!({ "width": w, "height": h, "data": data }),
            );
        }
        Err(e) => eprintln!("截图失败: {e}"),
    }
}

/* ======================= 长截图 ======================= */

/// 用 xdotool 把鼠标移到选区中心，滚动约 px 像素（向下）
fn scroll_wheel(x: i32, y: i32, px: u32) -> Result<(), String> {
    // GTK/Qt 滚轮单 tick 约 53px，按此估算 tick 数，实际偏移由模板匹配校正
    let ticks = (px / 53).max(1);
    let (mx, my, mt) = (x.to_string(), y.to_string(), ticks.to_string());
    let st = std::process::Command::new("xdotool")
        .arg("mousemove")
        .arg(&mx)
        .arg(&my)
        .arg("click")
        .arg("--repeat")
        .arg(&mt)
        .arg("5") // button 5 = 滚轮向下
        .status()
        .map_err(|e| format!("调用 xdotool 失败: {e}"))?;
    if !st.success() {
        return Err("xdotool 滚动失败".into());
    }
    Ok(())
}

/// 两帧是否几乎一致（平均每通道绝对差 < 2.5，隔 8px 采样）
fn frames_similar(a: &RgbaImage, b: &RgbaImage) -> bool {
    if a.width() != b.width() || a.height() != b.height() {
        return false;
    }
    let (w, h) = (a.width(), a.height());
    let ra = a.as_raw();
    let rb = b.as_raw();
    let mut sum: u64 = 0;
    let mut n: u64 = 0;
    let mut y = 0;
    while y < h {
        let mut x = 0;
        while x < w {
            let i = (y as usize) * (w as usize * 4) + (x as usize * 4);
            let da = &ra[i..i + 4];
            let db = &rb[i..i + 4];
            sum += (da[0] as i64 - db[0] as i64).unsigned_abs() as u64
                + (da[1] as i64 - db[1] as i64).unsigned_abs() as u64
                + (da[2] as i64 - db[2] as i64).unsigned_abs() as u64;
            n += 3;
            x += 8;
        }
        y += 8;
    }
    n > 0 && (sum as f64 / n as f64) < 2.5
}

/// 计算 cur 顶部条带在 last 中 y 偏移处的像素差异（隔 2px 采样）
fn strip_cost(last: &RgbaImage, cur: &RgbaImage, y: u32, strip_h: u32) -> u64 {
    let w = last.width();
    let lr = last.as_raw();
    let cr = cur.as_raw();
    let mut sum: u64 = 0;
    let mut x = 0;
    while x < w {
        let mut sy = 0;
        while sy < strip_h {
            let li = (y + sy) as usize * (w as usize * 4) + (x as usize * 4);
            let ci = sy as usize * (w as usize * 4) + (x as usize * 4);
            let ld = &lr[li..li + 4];
            let cd = &cr[ci..ci + 4];
            sum += (ld[0] as i64 - cd[0] as i64).unsigned_abs() as u64
                + (ld[1] as i64 - cd[1] as i64).unsigned_abs() as u64
                + (ld[2] as i64 - cd[2] as i64).unsigned_abs() as u64;
            sy += 2;
        }
        x += 2;
    }
    sum
}

/// 模板匹配对齐：滚动后内容上移，cur[y] ≈ last[y+d]。
/// 在 last 中搜索 cur 顶部条带的最佳匹配位置，返回偏移 d；
/// 匹配质量太差或内容重复度太高时返回 None。
fn align_offset(last: &RgbaImage, cur: &RgbaImage) -> Option<u32> {
    let (w, h) = (last.width(), last.height());
    if h <= STRIP_H + 4 {
        return None;
    }
    let max_d = ((h as f64) * 0.9) as u32; // 滚动量不超过 90% 视口
    let end = max_d.min(h - STRIP_H);
    let mut best_y = 0u32;
    let mut best_cost = u64::MAX;
    let mut sum_cost: u64 = 0;
    let mut cnt: u64 = 0;
    for y in 0..=end {
        let c = strip_cost(last, cur, y, STRIP_H);
        sum_cost += c;
        cnt += 1;
        if c < best_cost {
            best_cost = c;
            best_y = y;
        }
    }
    // 匹配质量门槛：平均每像素差 < 15（0-255 三通道合计）
    let n = (w / 2) as u64 * (STRIP_H / 2) as u64;
    if best_cost / n.max(1) > 15 {
        return None;
    }
    // 歧义检测：最佳与平均过于接近 → 内容重复度高，偏移不可信
    // （全 0 差异的纯色场景：best == avg == 0，同样视为不可信）
    let avg_cost = sum_cost / cnt.max(1);
    if best_cost as f64 >= avg_cost as f64 * 0.8 {
        return None;
    }
    Some(best_y)
}

/// 长截图主循环：滚动 → 截图 → 模板匹配对齐 → 动态拼接
fn long_capture_loop(
    app: &AppHandle,
    state: &LongCaptureState,
    x: u32,
    y: u32,
    w: u32,
    h: u32,
) -> Result<u32, String> {
    // 1. 退出全屏，把窗口挪到不与选区重叠的角落，显示进度条
    let win = app.get_webview_window("main").ok_or("找不到主窗口")?;
    let _ = win.set_fullscreen(false);
    let _ = win.set_size(tauri::Size::Physical(tauri::PhysicalSize {
        width: PROGRESS_W,
        height: PROGRESS_H,
    }));
    if let Ok(monitors) = xcap::Monitor::all() {
        if let Some(m) = monitors.iter().find(|m| m.is_primary().unwrap_or(false)) {
            let pos = corner_position(
                (x, y, w, h),
                m.width().unwrap_or(0),
                m.height().unwrap_or(0),
            );
            let _ = win.set_position(tauri::Position::Physical(tauri::PhysicalPosition {
                x: pos.0,
                y: pos.1,
            }));
        }
    }
    let _ = win.show();
    std::thread::sleep(Duration::from_millis(300)); // 等窗口布局稳定

    // 2. 截取第一帧（选区区域），作为拼接起点
    let mut last = capture_region(x, y, w, h)?;
    let mut buf: Vec<u8> = last.as_raw().clone();
    let mut frames: u32 = 1;
    let mouse_x = x as i32 + (w as i32) / 2;
    let mouse_y = y as i32 + (h as i32) / 2;
    let scroll_px = (h as f64 * (1.0 - SCROLL_OVERLAP)) as u32; // 每次滚动 80% 视口

    // 3. 循环：滚动 → 截图 → 对齐 → 拼接
    loop {
        if state.cancel.load(Ordering::SeqCst) {
            break;
        }
        if frames >= MAX_FRAMES {
            break;
        }
        if buf.len() as u32 / (w * 4) >= MAX_HEIGHT {
            break;
        }

        scroll_wheel(mouse_x, mouse_y, scroll_px)?;
        std::thread::sleep(Duration::from_millis(FRAME_SETTLE_MS));

        let cur = capture_region(x, y, w, h)?;

        // 连续两帧一致 → 已滚到底部
        if frames_similar(&last, &cur) {
            break;
        }

        // 模板匹配求偏移 d：cur 顶部内容在 last 中的位置
        let Some(d) = align_offset(&last, &cur) else {
            break; // 对齐失败（滚动异常/内容重复），用已有内容
        };
        if d == 0 {
            break; // 没有滚动
        }

        // 拼接新增部分 cur[d..h]
        let row_bytes = w as usize * 4;
        let raw = cur.as_raw();
        buf.extend_from_slice(&raw[d as usize * row_bytes..]);
        frames += 1;

        let _ = app.emit(
            "long-capture-progress",
            serde_json::json!({ "frames": frames, "height": buf.len() as u32 / (w * 4) }),
        );

        last = cur;
    }

    if buf.is_empty() {
        return Err("没有截到内容".into());
    }
    let total_h = buf.len() as u32 / (w * 4);
    let full = RgbaImage::from_raw(w, total_h, buf).ok_or("长图数据异常")?;

    // 4. 全图 PNG 缓存 + 缩略图
    let mut png: Vec<u8> = Vec::new();
    image::DynamicImage::ImageRgba8(full.clone())
        .write_to(&mut Cursor::new(&mut png), ImageFormat::Png)
        .map_err(|e| e.to_string())?;
    *state.last_long_png.lock().unwrap() = Some(png);

    let thumb_w = 640u32;
    let thumb_h = ((total_h as f64 * thumb_w as f64 / w as f64) as u32).max(1);
    let thumb = image::imageops::thumbnail(&full, thumb_w, thumb_h);
    let mut tpng: Vec<u8> = Vec::new();
    image::DynamicImage::ImageRgba8(thumb)
        .write_to(&mut Cursor::new(&mut tpng), ImageFormat::Png)
        .map_err(|e| e.to_string())?;

    let _ = app.emit(
        "long-capture-done",
        serde_json::json!({
            "width": w,
            "height": total_h,
            "frames": frames,
            "cancelled": state.cancel.load(Ordering::SeqCst),
            "preview": BASE64.encode(&tpng),
        }),
    );
    Ok(total_h)
}

/// 选一个不与选区重叠的屏幕角落（物理坐标）
fn corner_position(sel: (u32, u32, u32, u32), sw: u32, sh: u32) -> (i32, i32) {
    let (x, y, w, h) = sel;
    let candidates = [
        (sw.saturating_sub(PROGRESS_W) as i32, sh.saturating_sub(PROGRESS_H) as i32), // 右下
        (0, sh.saturating_sub(PROGRESS_H) as i32),                                    // 左下
        (sw.saturating_sub(PROGRESS_W) as i32, 0),                                    // 右上
        (0, 0),                                                                       // 左上
    ];
    for (cx, cy) in candidates {
        let overlap = cx < (x + w) as i32
            && (cx + PROGRESS_W as i32) > x as i32
            && cy < (y + h) as i32
            && (cy + PROGRESS_H as i32) > y as i32;
        if !overlap {
            return (cx, cy);
        }
    }
    candidates[0] // 全部角落都被覆盖（选区≈全屏），退回右下角
}

/// 生成当前时间戳文件名（YYYYMMDD-HHMMSS，无额外依赖）
fn timestamp() -> String {
    let secs = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0);
    let days = secs.div_euclid(86_400);
    let rem = secs.rem_euclid(86_400);
    let (hour, min, sec) = (rem / 3600, (rem % 3600) / 60, rem % 60);
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1_460 + doe / 36_524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let yy = if m <= 2 { y + 1 } else { y };
    format!("{yy:04}{m:02}{d:02}-{hour:02}{min:02}{sec:02}")
}

/// 开始长截图：后台线程执行滚动+截图+拼接循环
#[tauri::command]
fn start_long_capture(
    app: AppHandle,
    state: tauri::State<'_, Arc<LongCaptureState>>,
    x: u32,
    y: u32,
    w: u32,
    h: u32,
) -> Result<(), String> {
    if w < 50 || h < 50 {
        return Err("选区太小，长截图需要至少 50×50 的区域".into());
    }
    state.cancel.store(false, Ordering::SeqCst);
    *state.last_long_png.lock().unwrap() = None;

    let state = state.inner().clone();
    std::thread::spawn(move || {
        let result = long_capture_loop(&app, &state, x, y, w, h);
        // 无论成败都恢复窗口为预览尺寸并显示
        if let Some(win) = app.get_webview_window("main") {
            let _ = win.set_fullscreen(false);
            if let Ok(monitors) = xcap::Monitor::all() {
                if let Some(m) = monitors.iter().find(|m| m.is_primary().unwrap_or(false)) {
                    let pw = m.width().unwrap_or(1000).min(1000).max(600);
                    let ph = m.height().unwrap_or(720).saturating_sub(80).min(720).max(400);
                    let _ = win.set_size(tauri::Size::Physical(tauri::PhysicalSize {
                        width: pw,
                        height: ph,
                    }));
                    let _ = win.center();
                }
            }
            let _ = win.show();
            let _ = win.set_focus();
        }
        if let Err(e) = result {
            let _ = app.emit(
                "long-capture-failed",
                serde_json::json!({ "error": e }),
            );
        }
    });
    Ok(())
}

/// 取消进行中的长截图（停止滚动，保留已截取部分）
#[tauri::command]
fn cancel_long_capture(state: tauri::State<'_, Arc<LongCaptureState>>) {
    state.cancel.store(true, Ordering::SeqCst);
}

/// 把缓存的完整长图保存到 ~/Pictures，返回保存路径
#[tauri::command]
fn save_long_capture(
    app: AppHandle,
    state: tauri::State<'_, Arc<LongCaptureState>>,
) -> Result<String, String> {
    let png = state
        .last_long_png
        .lock()
        .unwrap()
        .take()
        .ok_or("没有可保存的长图")?;
    let dir = app
        .path()
        .picture_dir()
        .or_else(|_| app.path().home_dir())
        .map_err(|e| e.to_string())?;
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let path = dir.join(format!("Qutter-{}.png", timestamp()));
    std::fs::write(&path, &png).map_err(|e| e.to_string())?;
    Ok(path.to_string_lossy().into_owned())
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 生成一张带水平条纹+竖线的测试图，便于模板匹配区分
    fn test_frame(w: u32, h: u32, shift: u32) -> RgbaImage {
        let mut img = RgbaImage::new(w, h);
        for (px, p) in img.pixels_mut().enumerate() {
            let x = px as u32 % w;
            let y = px as u32 / w;
            let v = ((y + shift) % 200) as u8;
            let stripe = ((x / 40) % 3) as u8 * 60;
            *p = image::Rgba([v.wrapping_add(stripe), 120, 200 - v, 255]);
        }
        img
    }

    #[test]
    fn align_finds_scroll_offset() {
        let w = 320;
        let h = 240;
        let shift = 180u32; // 模拟向下滚动 180px：cur[y] == last[y+180]
        let last = test_frame(w, h, 0);
        let cur = test_frame(w, h, shift);
        let d = align_offset(&last, &cur).expect("应找到偏移");
        assert_eq!(d, shift, "模板匹配应给出精确滚动偏移");
    }

    #[test]
    fn similar_frames_detected() {
        let a = test_frame(200, 150, 5);
        let b = test_frame(200, 150, 5);
        assert!(frames_similar(&a, &b), "相同帧应判定为一致");

        let c = test_frame(200, 150, 90);
        assert!(!frames_similar(&a, &c), "不同内容不应判定为一致");
    }

    #[test]
    fn align_rejects_uniform_content() {
        // 纯色帧：任意偏移匹配度都相同 → 应判定为不可信
        let last = RgbaImage::from_pixel(200, 200, image::Rgba([10, 20, 30, 255]));
        let cur = RgbaImage::from_pixel(200, 200, image::Rgba([10, 20, 30, 255]));
        assert!(align_offset(&last, &cur).is_none(), "纯色内容不应产生可信偏移");
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(
            tauri_plugin_global_shortcut::Builder::new()
                .with_handler(|app, _shortcut, event| {
                    if event.state() == ShortcutState::Pressed {
                        start_capture(app.clone());
                    }
                })
                .build(),
        )
        .plugin(tauri_plugin_clipboard_manager::init())
        .manage(Arc::new(LongCaptureState::default()))
        .setup(|app| {
            use tauri_plugin_global_shortcut::GlobalShortcutExt;
            let shortcut = Shortcut::new(
                Some(Modifiers::CONTROL | Modifiers::SHIFT),
                Code::KeyA,
            );
            // 快捷键可能已被其他实例/应用占用：降级为警告，不阻塞启动
            if let Err(e) = app.global_shortcut().register(shortcut) {
                eprintln!("注册全局快捷键 Ctrl+Shift+A 失败（可能已被占用）: {e}");
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            copy_image,
            finish_capture,
            exit_app,
            start_long_capture,
            cancel_long_capture,
            save_long_capture
        ])
        .run(tauri::generate_context!())
        .expect("Qutter 启动失败");
}
