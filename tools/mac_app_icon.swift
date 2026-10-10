// Draws the 1024x1024 app icon used by tools/make_icons.sh.
// Usage: swift tools/mac_app_icon.swift out.png
import AppKit

let size = 1024
let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
                    space: CGColorSpaceCreateDeviceRGB(),
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
// Work in top-left coordinates like an image editor.
ctx.translateBy(x: 0, y: CGFloat(size))
ctx.scaleBy(x: 1, y: -1)

func rgb(_ hex: UInt32, _ a: CGFloat = 1) -> CGColor {
    CGColor(red: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
            blue: CGFloat(hex & 0xFF) / 255, alpha: a)
}

func rounded(_ r: CGRect, _ radius: CGFloat) -> CGPath {
    CGPath(roundedRect: r, cornerWidth: radius, cornerHeight: radius, transform: nil)
}

func fill(_ path: CGPath, _ color: CGColor) {
    ctx.addPath(path)
    ctx.setFillColor(color)
    ctx.fillPath()
}

func fillGradient(_ path: CGPath, _ top: CGColor, _ bottom: CGColor, _ r: CGRect) {
    ctx.saveGState()
    ctx.addPath(path)
    ctx.clip()
    let g = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: [top, bottom] as CFArray,
                       locations: [0, 1])!
    ctx.drawLinearGradient(g, start: CGPoint(x: r.midX, y: r.minY), end: CGPoint(x: r.midX, y: r.maxY),
                           options: [])
    ctx.restoreGState()
}

// Background tile on the standard macOS icon grid (824pt body, 100pt margin).
let tile = CGRect(x: 100, y: 100, width: 824, height: 824)
let tilePath = rounded(tile, 185)
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 24, color: rgb(0x000000, 0.35))
fill(tilePath, rgb(0x1B1C4A))
ctx.restoreGState()
fillGradient(tilePath, rgb(0x34358A), rgb(0x14153D), tile)

// Soft glow behind the console.
ctx.saveGState()
ctx.addPath(tilePath)
ctx.clip()
let glow = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(),
                      colors: [rgb(0x8F7CFF, 0.45), rgb(0x8F7CFF, 0)] as CFArray, locations: [0, 1])!
ctx.drawRadialGradient(glow, startCenter: CGPoint(x: 512, y: 500), startRadius: 0,
                       endCenter: CGPoint(x: 512, y: 500), endRadius: 420, options: [])
ctx.restoreGState()

// Shoulder buttons peek out above the body.
fill(rounded(CGRect(x: 205, y: 318, width: 150, height: 60), 26), rgb(0x7462E0))
fill(rounded(CGRect(x: 669, y: 318, width: 150, height: 60), 26), rgb(0x7462E0))

// Console body.
let body = CGRect(x: 152, y: 340, width: 720, height: 400)
let bodyPath = rounded(body, 150)
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -26), blur: 40, color: rgb(0x05051A, 0.6))
fill(bodyPath, rgb(0x6A57DA))
ctx.restoreGState()
fillGradient(bodyPath, rgb(0x9D8CFF), rgb(0x5641C9), body)
// Gloss along the top edge.
ctx.saveGState()
ctx.addPath(bodyPath)
ctx.clip()
fill(rounded(CGRect(x: 172, y: 348, width: 680, height: 150), 120), rgb(0xFFFFFF, 0.12))
ctx.restoreGState()

// Screen bezel and 3:2 screen.
fill(rounded(CGRect(x: 332, y: 405, width: 360, height: 262), 30), rgb(0x221C3F))
let screen = CGRect(x: 368, y: 437, width: 288, height: 192)
ctx.saveGState()
ctx.addPath(CGPath(rect: screen, transform: nil))
ctx.clip()

// Pixel-art scene: sky, sun, two hill layers, grass.
let px: CGFloat = 12
let cols = Int(screen.width / px), rows = Int(screen.height / px)
let sky: [UInt32] = [0x5EC8FF, 0x6DD0FF, 0x7FD8FF, 0x92E0FF, 0xA6E8FF, 0xBCEFFF]
for row in 0..<rows {
    let band = min(row * sky.count / max(rows - 6, 1), sky.count - 1)
    ctx.setFillColor(rgb(sky[band]))
    ctx.fill(CGRect(x: screen.minX, y: screen.minY + CGFloat(row) * px, width: screen.width, height: px))
}
for dy in -2...2 {
    for dx in -2...2 where abs(dx) + abs(dy) < 4 {
        ctx.setFillColor(rgb(abs(dx) + abs(dy) == 3 ? 0xFFB938 : 0xFFE066))
        ctx.fill(CGRect(x: screen.minX + CGFloat(18 + dx) * px, y: screen.minY + CGFloat(4 + dy) * px,
                        width: px, height: px))
    }
}
func hills(_ base: Double, _ amp: Double, _ freq: Double, _ phase: Double, _ top: UInt32, _ fillC: UInt32) {
    for c in 0..<cols {
        let h = Int((base + amp * sin(Double(c) * freq + phase)).rounded())
        for r in (rows - h)..<rows {
            ctx.setFillColor(rgb(r == rows - h ? top : fillC))
            ctx.fill(CGRect(x: screen.minX + CGFloat(c) * px, y: screen.minY + CGFloat(r) * px,
                            width: px, height: px))
        }
    }
}
hills(8, 2.5, 0.35, 0.5, 0x7BD88F, 0x4FB36A)
hills(4.5, 1.5, 0.55, 2.0, 0x5FD25B, 0x2E9E4A)
ctx.restoreGState()

// D-pad.
let dpad = CGPoint(x: 252, y: 540), arm: CGFloat = 58, thick: CGFloat = 40
let dpadColor = rgb(0x2B2257)
fill(rounded(CGRect(x: dpad.x - arm, y: dpad.y - thick / 2, width: arm * 2, height: thick), 10), dpadColor)
fill(rounded(CGRect(x: dpad.x - thick / 2, y: dpad.y - arm, width: thick, height: arm * 2), 10), dpadColor)

// A and B buttons.
for (center, color) in [(CGPoint(x: 800, y: 512), rgb(0xFF6FA5)), (CGPoint(x: 728, y: 580), rgb(0xFF9FC3))] {
    let r: CGFloat = 34
    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: -6), blur: 8, color: rgb(0x1A0F4A, 0.5))
    ctx.addEllipse(in: CGRect(x: center.x - r, y: center.y - r, width: r * 2, height: r * 2))
    ctx.setFillColor(color)
    ctx.fillPath()
    ctx.restoreGState()
}

// Start and select.
fill(rounded(CGRect(x: 214, y: 660, width: 34, height: 14), 7), dpadColor)
fill(rounded(CGRect(x: 262, y: 660, width: 34, height: 14), 7), dpadColor)

// Speaker grille.
for i in 0..<3 {
    for j in 0..<3 {
        ctx.addEllipse(in: CGRect(x: 768 + CGFloat(i) * 22, y: 640 + CGFloat(j) * 22, width: 9, height: 9))
    }
}
ctx.setFillColor(rgb(0x3A2B8F))
ctx.fillPath()

let out = URL(fileURLWithPath: CommandLine.arguments[1])
let rep = NSBitmapImageRep(cgImage: ctx.makeImage()!)
try! rep.representation(using: .png, properties: [:])!.write(to: out)
