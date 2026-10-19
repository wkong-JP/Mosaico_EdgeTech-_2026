#!/usr/bin/env python3
"""Generate high-resolution 16:9 cover images and promotional assets for Mosaico Ideas release."""
import os
import math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RELEASE_DIR = os.path.join(ROOT, "release")
os.makedirs(RELEASE_DIR, exist_ok=True)

# Dimensions
W, H = 1920, 1080

# Colors
C_BG0 = (6, 10, 18)
C_BG1 = (14, 24, 44)
C_ACCENT_CYAN = (0, 229, 255)
C_ACCENT_GOLD = (255, 179, 0)
C_TEXT_WHITE = (245, 247, 250)
C_TEXT_MUTED = (148, 163, 184)
C_CARD_BG = (15, 26, 46)
C_CARD_BORDER = (30, 58, 95)

# Fonts
FONT_HEADING = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
FONT_TEXT = "/System/Library/Fonts/Supplemental/Arial.ttf"
FONT_CN = "/System/Library/Fonts/Hiragino Sans GB.ttc"

def get_font(path, size, index=0):
    try:
        return ImageFont.truetype(path, size, index=index)
    except Exception:
        return ImageFont.load_default()

def draw_device_mockup(screen_img, corner_radius=28, border_width=10):
    """Wrap a 480x480 screen image into a sleek rounded device frame with glass border."""
    sw, sh = screen_img.size
    dw, dh = sw + border_width * 2, sh + border_width * 2
    dev = Image.new("RGBA", (dw, dh), (0, 0, 0, 0))
    d_draw = ImageDraw.Draw(dev)
    
    # Outer bezel
    d_draw.rounded_rectangle([0, 0, dw, dh], radius=corner_radius + border_width, fill=(20, 26, 38, 255), outline=(50, 70, 100, 255), width=2)
    
    # Mask screen to rounded corners
    mask = Image.new("L", (sw, sh), 0)
    m_draw = ImageDraw.Draw(mask)
    m_draw.rounded_rectangle([0, 0, sw, sh], radius=corner_radius, fill=255)
    
    dev.paste(screen_img, (border_width, border_width), mask)
    
    # Inner border line
    d_draw.rounded_rectangle([border_width, border_width, border_width + sw, border_width + sh], radius=corner_radius, outline=(0, 229, 255, 60), width=1)
    return dev

def make_hero_cover():
    print("Generating Hero Cover (1920x1080)...")
    im = Image.new("RGBA", (W, H), C_BG0)
    draw = ImageDraw.Draw(im)

    # Gradient background
    for y in range(H):
        t = y / H
        r = int(C_BG0[0] * (1 - t) + C_BG1[0] * t)
        g = int(C_BG0[1] * (1 - t) + C_BG1[1] * t)
        b = int(C_BG0[2] * (1 - t) + C_BG1[2] * t)
        draw.line([(0, y), (W, y)], fill=(r, g, b, 255))

    # Grid background
    grid_col = (0, 229, 255, 12)
    grid_img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(grid_img)
    for x in range(0, W, 48):
        gdraw.line([(x, 0), (x, H)], fill=grid_col)
    for y in range(0, H, 48):
        gdraw.line([(0, y), (W, y)], fill=grid_col)
    im.alpha_composite(grid_img)

    # Glow in upper right
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    glow_draw = ImageDraw.Draw(glow)
    glow_draw.ellipse([1100, 100, 1900, 900], fill=(0, 229, 255, 30))
    glow_draw.ellipse([300, 700, 900, 1300], fill=(255, 179, 0, 20))
    glow = glow.filter(ImageFilter.GaussianBlur(100))
    im.alpha_composite(glow)

    # Espressif Official Logo in Top-Left
    logo_path = os.path.join(ROOT, "..", "Espressif_Logo_EN.png")
    if os.path.exists(logo_path):
        logo = Image.open(logo_path).convert("RGBA")
        # Resize preserving aspect ratio (height ~48px)
        lh = 46
        lw = int(logo.width * (lh / logo.height))
        logo = logo.resize((lw, lh), Image.Resampling.LANCZOS)
        im.paste(logo, (96, 72), logo)

    # Category Pill
    pill_font = get_font(FONT_HEADING, 20)
    pill_text = "EDGETECH+ 2026 FLAGSHIP RELEASE"
    pw = pill_font.getlength(pill_text) + 32
    draw.rounded_rectangle([96, 150, 96 + pw, 188], radius=19, fill=(0, 229, 255, 30), outline=C_ACCENT_CYAN, width=2)
    draw.text((112, 158), pill_text, font=pill_font, fill=C_ACCENT_CYAN)

    # Main Titles
    title_font = get_font(FONT_HEADING, 76)
    draw.text((96, 216), "ESP-MOSAICO", font=title_font, fill=C_TEXT_WHITE)
    draw.text((96, 300), "EXPERIENCE", font=title_font, fill=C_ACCENT_CYAN)

    subtitle_font = get_font(FONT_CN, 32, index=0)
    draw.text((96, 400), "全能离线旗舰体验固件 · 充分挖掘 16MB PSRAM", font=subtitle_font, fill=(226, 232, 240))
    
    en_sub_font = get_font(FONT_TEXT, 22)
    draw.text((96, 452), "Multi-Modal AIoT Experience with Dual-Language Voice & Vision", font=en_sub_font, fill=C_TEXT_MUTED)

    # Feature Bullet Chips
    chips = [
        ("ESP32-S31", "Dual-Core RISC-V @ 240MHz"),
        ("16 MB Octal PSRAM", "250 MHz Ultra-High Speed"),
        ("Offline Voice Control", "Dual Wake ('Hi ESP' / 'こんにちは')"),
        ("SC101IOT Camera", "Hand Gesture Tracking with Comet Trail"),
        ("480x480 AMOLED", "Optimized Curved-Corner UI"),
        ("100% Zero-RF Verified", "Audit Passed: Wi-Fi/BT Off"),
    ]
    chip_y = 516
    c_title_font = get_font(FONT_HEADING, 18)
    c_desc_font = get_font(FONT_TEXT, 15)
    for i, (head, sub) in enumerate(chips):
        row = i // 2
        col = i % 2
        cx = 96 + col * 390
        cy = chip_y + row * 68
        draw.rounded_rectangle([cx, cy, cx + 370, cy + 56], radius=12, fill=(15, 26, 46, 220), outline=C_CARD_BORDER, width=1)
        # Accent icon dot
        draw.ellipse([cx + 16, cy + 18, cx + 24, cy + 26], fill=C_ACCENT_CYAN if col == 0 else C_ACCENT_GOLD)
        draw.text((cx + 36, cy + 8), head, font=c_title_font, fill=C_TEXT_WHITE)
        draw.text((cx + 36, cy + 30), sub, font=c_desc_font, fill=C_TEXT_MUTED)

    # Bottom Flash Note
    note_font = get_font(FONT_TEXT, 18)
    draw.text((96, 750), "Single Full Binary at 0x0  ·  Ready to flash on Mosaico V1.0 / V1.1 / V1.2", font=note_font, fill=(100, 116, 139))

    # Right side: Devices Composition
    # Load screens: Home, Game (gesture), Audio (spectrum)
    scr_game_p = os.path.join(ROOT, "evidence/screenshots/game_playing_face.png")
    scr_home_p = os.path.join(ROOT, "evidence/screenshots/home.png")
    scr_audio_p = os.path.join(ROOT, "evidence/screenshots/audio_voice.png")

    if os.path.exists(scr_game_p) and os.path.exists(scr_home_p):
        scr_game = Image.open(scr_game_p).convert("RGBA")
        scr_home = Image.open(scr_home_p).convert("RGBA")
        scr_audio = Image.open(scr_audio_p).convert("RGBA") if os.path.exists(scr_audio_p) else scr_home

        # Mockup 1: Audio (Left background, scaled 360x360)
        dev_audio = draw_device_mockup(scr_audio).resize((360, 360), Image.Resampling.LANCZOS)
        # Shadow for dev_audio
        im.paste(dev_audio, (1000, 200), dev_audio)

        # Mockup 2: Home (Right background, scaled 380x380)
        dev_home = draw_device_mockup(scr_home).resize((380, 380), Image.Resampling.LANCZOS)
        im.paste(dev_home, (1440, 240), dev_home)

        # Mockup 3: Hero Game with Camera tracking (Center foreground, 540x540)
        dev_game = draw_device_mockup(scr_game).resize((540, 540), Image.Resampling.LANCZOS)
        # Drop shadow
        shadow = Image.new("RGBA", (580, 580), (0, 0, 0, 0))
        s_draw = ImageDraw.Draw(shadow)
        s_draw.rounded_rectangle([20, 20, 560, 560], radius=40, fill=(0, 0, 0, 180))
        shadow = shadow.filter(ImageFilter.GaussianBlur(25))
        im.paste(shadow, (1160, 390), shadow)
        im.paste(dev_game, (1180, 410), dev_game)

    # Save PNG and WEBP
    out_png = os.path.join(RELEASE_DIR, "cover_mosaico_experience.png")
    out_webp = os.path.join(RELEASE_DIR, "cover_mosaico_experience.webp")
    im.save(out_png, "PNG")
    im.save(out_webp, "WEBP", quality=92)
    print(f"Saved: {out_png} ({os.path.getsize(out_png)} bytes)")
    print(f"Saved: {out_webp} ({os.path.getsize(out_webp)} bytes)")

def make_screens_showcase():
    print("Generating Screens Showcase (1920x1080)...")
    im = Image.new("RGBA", (W, H), C_BG0)
    draw = ImageDraw.Draw(im)

    # Gradient background
    for y in range(H):
        t = y / H
        r = int(C_BG0[0] * (1 - t) + C_BG1[0] * t)
        g = int(C_BG0[1] * (1 - t) + C_BG1[1] * t)
        b = int(C_BG0[2] * (1 - t) + C_BG1[2] * t)
        draw.line([(0, y), (W, y)], fill=(r, g, b, 255))

    # Header
    title_font = get_font(FONT_HEADING, 44)
    sub_font = get_font(FONT_CN, 24)
    draw.text((96, 60), "ESP-MOSAICO EXPERIENCE — 五大功能界面", font=title_font, fill=C_TEXT_WHITE)
    draw.text((96, 120), "480×480 AMOLED 全界面英文国际化与精细圆角排版，充分调用 16MB 高速 PSRAM", font=sub_font, fill=C_ACCENT_CYAN)

    screens_meta = [
        ("Home 首页", "系统导航 / 磁吸状态", "home.png"),
        ("Motion 姿态", "BMI270 + 双磁力计", "motion_compass_calibrated.png"),
        ("Audio & AI", "瀑布频谱 / 离线语音", "audio_voice.png"),
        ("Tilt Maze 迷宫", "手势彗星尾迹 / 重力", "game_playing_face.png"),
        ("Device 设备", "16MB PSRAM / 架构看板", "device_modules.png"),
    ]

    card_w = 320
    card_h = 320
    start_x = 76
    gap_x = 44
    card_y = 200

    title_f = get_font(FONT_HEADING, 20)
    desc_f = get_font(FONT_CN, 16)

    for i, (title, desc, filename) in enumerate(screens_meta):
        cx = start_x + i * (card_w + gap_x)
        p = os.path.join(ROOT, "evidence/screenshots", filename)
        if not os.path.exists(p):
            p = os.path.join(ROOT, "evidence/screenshots/home.png")
        simg = Image.open(p).convert("RGBA")
        mock = draw_device_mockup(simg, corner_radius=20, border_width=6).resize((card_w, card_h), Image.Resampling.LANCZOS)
        
        # Shadow
        sh = Image.new("RGBA", (card_w + 30, card_h + 30), (0, 0, 0, 0))
        sh_draw = ImageDraw.Draw(sh)
        sh_draw.rounded_rectangle([10, 10, card_w + 20, card_h + 20], radius=24, fill=(0, 0, 0, 140))
        sh = sh.filter(ImageFilter.GaussianBlur(15))
        im.paste(sh, (cx - 5, card_y + 10), sh)
        im.paste(mock, (cx, card_y), mock)

        # Labels below
        draw.rounded_rectangle([cx, card_y + card_h + 24, cx + card_w, card_y + card_h + 96], radius=12, fill=(15, 26, 46, 200), outline=C_CARD_BORDER)
        draw.text((cx + 16, card_y + card_h + 36), title, font=title_f, fill=C_TEXT_WHITE)
        draw.text((cx + 16, card_y + card_h + 64), desc, font=desc_f, fill=C_TEXT_MUTED)

    # Bottom bar
    bot_font = get_font(FONT_TEXT, 20)
    draw.text((W // 2, 700), "Powered by Espressif Systems (Shanghai) Co., Ltd.  ·  Strict Zero-RF Offline Firmware", font=bot_font, fill=(148, 163, 184), anchor="mm")

    out_png = os.path.join(RELEASE_DIR, "cover_screens_showcase.png")
    out_webp = os.path.join(RELEASE_DIR, "cover_screens_showcase.webp")
    im.save(out_png, "PNG")
    im.save(out_webp, "WEBP", quality=92)
    print(f"Saved: {out_png} ({os.path.getsize(out_png)} bytes)")
    print(f"Saved: {out_webp} ({os.path.getsize(out_webp)} bytes)")

if __name__ == "__main__":
    make_hero_cover()
    make_screens_showcase()
    print("All cover images generated successfully!")
