import AppKit
import ServiceManagement
import Darwin

// A native AppKit application. No polling, web view, or external runtime.
let appName = "豆泥日历"

struct Day: Hashable {
    let year: Int
    let month: Int
    let day: Int
    static var calendar: Calendar {
        var value = Calendar(identifier: .gregorian)
        value.timeZone = .autoupdatingCurrent
        return value
    }
    init(_ year: Int, _ month: Int, _ day: Int) {
        self.year = year; self.month = month; self.day = day
    }
    init(_ date: Date) {
        let parts = Self.calendar.dateComponents([.year, .month, .day], from: date)
        self.init(parts.year!, parts.month!, parts.day!)
    }
    var date: Date? { Self.calendar.date(from: DateComponents(year: year, month: month, day: day, hour: 12)) }
    var valid: Bool {
        guard (1...9999).contains(year), (1...12).contains(month), let date = date else { return false }
        return Day(date) == self
    }
    var fileMonth: String { String(format: "%04d-%02d", year, month) }
    var fileDay: String { String(format: "%02d.txt", day) }
    var weekday: Int { date.map { (Self.calendar.component(.weekday, from: $0) + 5) % 7 } ?? 0 }
    static var today: Day { Day(Date()) }
    func adding(_ count: Int) -> Day {
        guard let date = date, let result = Self.calendar.date(byAdding: .day, value: count, to: date) else { return self }
        return Day(result)
    }
    static func cells(year: Int, month: Int) -> [Day] {
        let first = Day(year, month, 1)
        return (0..<42).map { first.adding($0 - first.weekday) }
    }
}

struct Settings: Codable {
    var x: Double? = nil
    var y: Double? = nil
    var width: Double = 980
    var height: Double = 660
    var year: Int = Day.today.year
    var month: Int = Day.today.month
    var followToday = true
    var handwritten = false
    var showHolidays = true
    var noteFontSize = 12
    var valid: Bool {
        (1601...9999).contains(year) && (1...12).contains(month) &&
        (280...10000).contains(width) && (240...10000).contains(height) &&
        (8...32).contains(noteFontSize) && width.isFinite && height.isFinite &&
        (x == nil || (x!.isFinite && abs(x!) <= 100000)) &&
        (y == nil || (y!.isFinite && abs(y!) <= 100000))
    }
}

enum StoreError: Error { case invalidData, invalidDate, invalidSettings }
final class Store {
    let root: URL
    private var settingsHealthy = true
    init(directory: URL) { root = directory.appendingPathComponent("data", isDirectory: true) }
    func read(_ day: Day) throws -> String {
        guard day.valid else { throw StoreError.invalidDate }
        let path = noteURL(day)
        guard FileManager.default.fileExists(atPath: path.path) else { return "" }
        let data = try Data(contentsOf: path, options: .mappedIfSafe)
        guard data.count <= 1024 * 1024, let text = String(data: data, encoding: .utf8), !text.contains("\0") else { throw StoreError.invalidData }
        return text.hasPrefix("\u{FEFF}") ? String(text.dropFirst()) : text
    }
    func noteURL(_ day: Day, backup: Bool = false) -> URL {
        root.appendingPathComponent(backup ? "backup" : "notes", isDirectory: true)
            .appendingPathComponent(day.fileMonth, isDirectory: true).appendingPathComponent(day.fileDay)
    }
    private func write(_ data: Data, to path: URL) throws {
        try FileManager.default.createDirectory(at: path.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: path, options: .atomic)
    }
    func save(_ text: String, for day: Day) throws {
        let bytes = Data(text.utf8)
        guard bytes.count <= 1024 * 1024, !text.contains("\0") else { throw StoreError.invalidData }
        let previous = try read(day)
        if previous == text { return }
        if FileManager.default.fileExists(atPath: noteURL(day).path) {
            try write(Data(previous.utf8), to: noteURL(day, backup: true))
        }
        try write(bytes, to: noteURL(day))
    }
    func loadSettings() -> Settings {
        let path = root.appendingPathComponent("settings-macos.json")
        guard FileManager.default.fileExists(atPath: path.path) else { return Settings() }
        do {
            let data = try Data(contentsOf: path)
            guard data.count <= 16384 else { throw StoreError.invalidSettings }
            let value = try JSONDecoder().decode(Settings.self, from: data)
            guard value.valid else { throw StoreError.invalidSettings }
            return value
        } catch { settingsHealthy = false; return Settings() }
    }
    func saveSettings(_ settings: Settings) throws {
        guard settingsHealthy, settings.valid else { throw StoreError.invalidSettings }
        let path = root.appendingPathComponent("settings-macos.json")
        if FileManager.default.fileExists(atPath: path.path) {
            let old = try Data(contentsOf: path)
            guard old.count <= 16384, let prior = try? JSONDecoder().decode(Settings.self, from: old), prior.valid else {
                settingsHealthy = false; throw StoreError.invalidSettings
            }
        }
        try write(JSONEncoder().encode(settings), to: path)
    }
}

// Solar dates are fixed; lunar names use the OS Chinese calendar. Qingming's
// exact 1901...2100 dates match the HKO table used by the Windows version.
enum Holidays {
    static let qingming: [UInt8] = Array("56655665566555655565556555655565556555655565555555555555555555555555555555545554555455545554555455545554555445544554455445544554455445544554445444544454445444544454445444544444444444444444444444444445".utf8)
    static func label(_ day: Day) -> String {
        var names: [String] = []
        if day.month == 1 && day.day == 1 { names.append("元旦") }
        if day.month == 5 && day.day == 1 { names.append("劳动节") }
        if day.month == 10 && day.day == 1 { names.append("国庆") }
        guard let date = day.date, (1901...2100).contains(day.year) else { return names.joined(separator: "·") }
        var lunar = Calendar(identifier: .chinese); lunar.timeZone = .autoupdatingCurrent
        let parts = lunar.dateComponents([.month, .day, .isLeapMonth], from: date)
        if parts.isLeapMonth != true {
            switch (parts.month ?? 0, parts.day ?? 0) {
            case (1, 1): names.append("春节")
            case (1, 15): names.append("元宵")
            case (5, 5): names.append("端午")
            case (8, 15): names.append("中秋")
            default: break
            }
        }
        if day.month == 4, qingming.count == 200, day.day == Int(qingming[day.year - 1901] - 48) { names.append("清明") }
        return names.joined(separator: "·")
    }
}

enum Mode { case locked, edit, transform }
enum EditKind { case note, year, month, fontSize }
struct Cell { var day: Day; var note: String; var holiday: String; var readable: Bool }
struct Layout {
    let size: NSSize
    var s: CGFloat { min(size.width / 980, size.height / 660) }
    var pad: CGFloat { 22 * s }
    var top: CGFloat { 86 * s }
    var col: CGFloat { (size.width - 2 * pad) / 7 }
    var row: CGFloat { (size.height - top - 22 * s) / 6 }
    func box(_ x: CGFloat, _ y: CGFloat, _ width: CGFloat, _ height: CGFloat) -> NSRect {
        NSRect(x: x, y: y, width: width, height: height)
    }
    var previous: NSRect { box(pad, 15*s, 25*s, 31*s) }
    var year: NSRect { box(pad+30*s, 8*s, 76*s, 43*s) }
    var month: NSRect { box(pad+136*s, 8*s, 40*s, 43*s) }
    var next: NSRect { box(pad+212*s, 15*s, 26*s, 31*s) }
    var today: NSRect { box(pad+250*s, 13*s, 33*s, 35*s) }
    var weekday: NSRect { box(pad+300*s, 22*s, 90*s, 24*s) }
    var done: NSRect { box(size.width-pad-59*s, 16*s, 59*s, 29*s) }
    var fontSize: NSRect { box(size.width-pad-115*s, 17*s, 40*s, 29*s) }
    func cell(_ index: Int) -> NSRect { box(pad+CGFloat(index%7)*col, top+CGFloat(index/7)*row, col, row) }
    func note(_ index: Int) -> NSRect {
        let rect = cell(index)
        return NSRect(x: rect.minX+9*s, y: rect.minY+37*s, width: rect.width-18*s, height: rect.height-43*s)
    }
}

final class CalendarWindow: NSWindow {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { false }
}

final class InlineTextView: NSTextView {
    weak var controller: AppController?
    var kind: EditKind = .note
    override func keyDown(with event: NSEvent) {
        if !hasMarkedText() {
            if event.keyCode == 53 { _ = controller?.finishEditing(save: false); return }
            if event.keyCode == 36 || event.keyCode == 76 {
                if kind != .note || !event.modifierFlags.contains(.shift) {
                    _ = controller?.finishEditing(save: true); return
                }
            }
        }
        super.keyDown(with: event)
    }
}

final class CalendarView: NSView {
    weak var controller: AppController?
    private var resizeEdges = 0
    private var resizeStart = NSRect.zero
    private var mouseStart = NSPoint.zero
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override func draw(_ dirtyRect: NSRect) {
        guard let app = controller else { return }
        NSColor.clear.setFill(); bounds.fill(using: .copy)
        let l = Layout(size: bounds.size), s = l.s
        if app.mode != .locked { NSColor(calibratedRed: 0.05, green: 0.11, blue: 0.15, alpha: 0.10).setFill(); bounds.fill() }
        func color(_ alpha: CGFloat, _ red: CGFloat = 0.94, _ green: CGFloat = 0.97, _ blue: CGFloat = 0.98) -> NSColor {
            NSColor(calibratedRed: red, green: green, blue: blue, alpha: alpha)
        }
        func text(_ value: String, _ size: CGFloat, _ rect: NSRect, _ ink: NSColor, centered: Bool = false) {
            guard !value.isEmpty else { return }
            let paragraph = NSMutableParagraphStyle()
            paragraph.alignment = centered ? .center : .left
            paragraph.lineBreakMode = .byTruncatingTail
            let font = app.font(size: size*s, numeric: value.unicodeScalars.allSatisfy { $0.isASCII })
            let shadow = NSShadow(); shadow.shadowColor = NSColor.black.withAlphaComponent(ink.alphaComponent * 0.45)
            shadow.shadowOffset = NSSize(width: 0.6*s, height: -0.7*s); shadow.shadowBlurRadius = 0
            let attributes: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: ink, .paragraphStyle: paragraph, .shadow: shadow]
            (value as NSString).draw(with: rect, options: [.usesLineFragmentOrigin, .usesFontLeading], attributes: attributes)
        }
        let title = color(205/255)
        if app.editor?.kind != .year { text(String(app.settings.year), 20, l.year, title, centered: true) }
        text("年",20,l.box(l.pad+108*s,8*s,24*s,43*s),title)
        if app.editor?.kind != .month { text(String(app.settings.month),20,l.month,title,centered:true) }
        text("月",20,l.box(l.pad+180*s,8*s,24*s,43*s),title)
        text(["星期一","星期二","星期三","星期四","星期五","星期六","星期日"][app.today.weekday],11.5,l.weekday,color(156/255))
        if app.mode != .locked {
            arrow(l.previous, forward: false, app: app); arrow(l.next, forward: true, app: app); alarm(l.today, app: app)
            text("完成",12,l.done,color(235/255),centered:true)
            stroke(NSPoint(x:l.done.minX,y:l.done.maxY),NSPoint(x:l.done.maxX,y:l.done.maxY),color:app.saveFailed ? .systemRed : color(0.28),width:0.7*s,sketch:app.settings.handwritten)
            if app.editor?.kind == .fontSize { text("字号",9.5,l.box(l.fontSize.minX-31*s,24*s,29*s,20*s),color(0.65)) }
        }
        let week = ["周一","周二","周三","周四","周五","周六","周日"]
        for c in 0..<7 {
            text(week[c],11.5,l.box(l.pad+CGFloat(c)*l.col+9*s,62*s,l.col,23*s),c>=5 ? color(0.69,0.96,0.85,0.64) : color(0.65))
        }
        for row in 0...6 { stroke(NSPoint(x:l.pad,y:l.top+CGFloat(row)*l.row),NSPoint(x:bounds.width-l.pad,y:l.top+CGFloat(row)*l.row),color:color(58/255),width:0.7*s,sketch:app.settings.handwritten,seed:row) }
        for col in 1..<7 { stroke(NSPoint(x:l.pad+CGFloat(col)*l.col,y:l.top),NSPoint(x:l.pad+CGFloat(col)*l.col,y:l.top+6*l.row),color:color(58/255),width:0.7*s,sketch:app.settings.handwritten,seed:col+8) }
        for (i, cell) in app.cells.enumerated() {
            let rect = l.cell(i), current = cell.day.year == app.settings.year && cell.day.month == app.settings.month
            if cell.day == app.today { star(NSPoint(x:rect.minX+23*s,y:rect.minY+20*s),radius:20*s) }
            text(String(format:"%02d",cell.day.day),15,l.box(rect.minX+8*s,rect.minY+9*s,31*s,26*s),cell.day == app.today ? color(0.94,1,0.86,0.56) : color(current ? 235/255 : 112/255),centered:true)
            text(cell.holiday,9.5,l.box(rect.minX+44*s,rect.minY+14*s,l.col-51*s,19*s),color(current ? 116/255 : 76/255,0.93,0.89,0.79))
            if !(app.editor?.kind == .note && app.editingDay == cell.day) {
                text(cell.note,CGFloat(app.settings.noteFontSize),l.note(i),color(current ? 235/255 : 116/255))
            }
        }
        if app.mode == .transform {
            for n in 0..<3 { stroke(NSPoint(x:bounds.maxX-(8+CGFloat(n)*5)*s,y:bounds.maxY-5*s),NSPoint(x:bounds.maxX-5*s,y:bounds.maxY-(8+CGFloat(n)*5)*s),color:color(0.65),width:s,sketch:app.settings.handwritten) }
        }
    }
    private func stroke(_ a: NSPoint, _ b: NSPoint, color: NSColor, width: CGFloat, sketch: Bool = false, seed: Int = 0) {
        let path = NSBezierPath(); path.move(to:a); path.lineWidth=width; path.lineCapStyle = .round
        if sketch {
            let dx=b.x-a.x,dy=b.y-a.y,length=max(1,hypot(dx,dy))
            for n in 1...4 {
                let t=CGFloat(n)/4,offset=n==4 ? 0 : sin(CGFloat(seed+n*7))*0.65*Layout(size:bounds.size).s
                path.line(to:NSPoint(x:a.x+dx*t-dy/length*offset,y:a.y+dy*t+dx/length*offset))
            }
        } else { path.line(to:b) }
        color.setStroke(); path.stroke()
    }
    private func star(_ center: NSPoint, radius: CGFloat) {
        let path=NSBezierPath()
        for n in 0..<10 {
            let angle = -CGFloat.pi/2+CGFloat(n)*CGFloat.pi/5
            let r=radius*(n%2 == 0 ? 1 : 0.58)
            let point=NSPoint(x:center.x+cos(angle)*r,y:center.y+sin(angle)*r)
            if n==0 { path.move(to:point) } else { path.line(to:point) }
        }
        path.close(); path.lineWidth=1.8*Layout(size:bounds.size).s; path.lineJoinStyle = .round
        NSColor(calibratedRed:1,green:0.85,blue:0.45,alpha:0.96).setStroke(); path.stroke()
        let shifted=path.copy() as! NSBezierPath; let transform=AffineTransform(translationByX:0.7,yBy:0.7); shifted.transform(using:transform)
        shifted.lineWidth=0.7*Layout(size:bounds.size).s; NSColor(calibratedRed:1,green:0.91,blue:0.64,alpha:0.5).setStroke(); shifted.stroke()
    }
    private func arrow(_ rect:NSRect,forward:Bool,app:AppController) {
        let s=Layout(size:bounds.size).s,d:CGFloat=forward ? 1 : -1
        let a=NSPoint(x:rect.midX-d*3*s,y:rect.midY-5*s),b=NSPoint(x:rect.midX+d*3*s,y:rect.midY),c=NSPoint(x:rect.midX-d*3*s,y:rect.midY+5*s)
        stroke(a,b,color:.white.withAlphaComponent(0.85),width:1.2*s,sketch:app.settings.handwritten)
        stroke(b,c,color:.white.withAlphaComponent(0.85),width:1.2*s,sketch:app.settings.handwritten)
    }
    private func alarm(_ rect:NSRect,app:AppController) {
        let s=Layout(size:bounds.size).s,c=NSPoint(x:rect.midX,y:rect.midY),r=7.3*s
        let path=NSBezierPath(ovalIn:NSRect(x:c.x-r,y:c.y-r,width:2*r,height:2*r)); path.lineWidth=1.1*s
        NSColor.white.withAlphaComponent(0.85).setStroke(); path.stroke()
        let segments:[(NSPoint,NSPoint)]=[(NSPoint(x:c.x,y:c.y),NSPoint(x:c.x,y:c.y-4*s)),(NSPoint(x:c.x,y:c.y),NSPoint(x:c.x+3*s,y:c.y+2*s)),(NSPoint(x:c.x-r-2*s,y:c.y-r+s),NSPoint(x:c.x-r+2*s,y:c.y-r-3*s)),(NSPoint(x:c.x+r-2*s,y:c.y-r-3*s),NSPoint(x:c.x+r+2*s,y:c.y-r+s)),(NSPoint(x:c.x-r*0.65,y:c.y+r*0.8),NSPoint(x:c.x-r,y:c.y+r+3*s)),(NSPoint(x:c.x+r*0.65,y:c.y+r*0.8),NSPoint(x:c.x+r,y:c.y+r+3*s))]
        for (a,b) in segments { stroke(a,b,color:.white.withAlphaComponent(0.85),width:1.1*s,sketch:true) }
    }
    override func mouseDown(with event:NSEvent) {
        guard let app=controller,app.mode != .locked,let window=window else { return }
        let point=convert(event.locationInWindow,from:nil),l=Layout(size:bounds.size)
        if l.done.contains(point) { app.finish(); return }
        if l.previous.contains(point) { app.stepMonth(-1); return }
        if l.next.contains(point) { app.stepMonth(1); return }
        if l.today.contains(point) { app.goToday(); return }
        if l.year.contains(point) { app.beginEditor(.year); return }
        if l.month.contains(point) { app.beginEditor(.month); return }
        if app.mode == .transform {
            guard app.finishEditing(save:true) else { return }
            let edge=max(9,11*l.s)
            resizeEdges=(point.x<edge ? 1:0)|(point.x>=bounds.width-edge ? 2:0)|(point.y<edge ? 4:0)|(point.y>=bounds.height-edge ? 8:0)
            if point.x>=bounds.width-28*l.s && point.y>=bounds.height-28*l.s { resizeEdges=10 }
            if resizeEdges==0 { app.moving=true; window.performDrag(with:event); app.moving=false; app.saveSettings(); return }
            resizeStart=window.frame; mouseStart=NSEvent.mouseLocation; return
        }
        if point.x>=l.pad && point.x<bounds.width-l.pad && point.y>=l.top && point.y<l.top+6*l.row {
            app.beginEditor(.note,index:Int((point.y-l.top)/l.row)*7+Int((point.x-l.pad)/l.col))
        } else { _=app.finishEditing(save:true) }
    }
    override func mouseDragged(with event:NSEvent) {
        guard resizeEdges != 0,let window=window else { return }
        let current=NSEvent.mouseLocation,dx=current.x-mouseStart.x,dy=current.y-mouseStart.y
        var frame=resizeStart
        if resizeEdges&1 != 0 { frame.origin.x+=dx; frame.size.width-=dx }
        if resizeEdges&2 != 0 { frame.size.width+=dx }
        if resizeEdges&4 != 0 { frame.size.height+=dy }
        if resizeEdges&8 != 0 { frame.origin.y+=dy; frame.size.height-=dy }
        if frame.width>=560 && frame.height>=420 { window.setFrame(frame,display:true) }
    }
    override func mouseUp(with event:NSEvent) {
        if resizeEdges != 0 { resizeEdges=0; controller?.clampWindow(); controller?.saveSettings() }
    }
    override func keyDown(with event:NSEvent) { if event.keyCode==53 { controller?.finish() } else { super.keyDown(with:event) } }
    override func setFrameSize(_ newSize:NSSize) { super.setFrameSize(newSize); controller?.positionEditor(); needsDisplay=true }
}

final class AppController: NSObject,NSApplicationDelegate,NSWindowDelegate,NSTextViewDelegate {
    let store:Store
    var settings:Settings
    var today=Day.today
    var cells:[Cell]=[]
    var mode:Mode = .locked
    var window:CalendarWindow!
    var canvas:CalendarView!
    var status:NSStatusItem!
    var editor:InlineTextView?
    var editingDay:Day?
    var originalYear=0,originalMonth=0
    var originalFollow=true
    var saveFailed=false
    var moving=false
    private var finishing=false
    private var midnight:Timer?
    private var observers:[NSObjectProtocol]=[]
    override init() {
        let bundle=Bundle.main.bundleURL
        let directory=bundle.pathExtension == "app" ? bundle.deletingLastPathComponent() : URL(fileURLWithPath:CommandLine.arguments[0]).deletingLastPathComponent()
        let storage=Store(directory:directory)
        store=storage; settings=storage.loadSettings(); super.init()
    }
    func applicationDidFinishLaunching(_ notification:Notification) {
        if settings.followToday { settings.year=today.year; settings.month=today.month }
        let screen=NSScreen.main?.visibleFrame ?? NSRect(x:0,y:0,width:1440,height:900)
        let width=min(CGFloat(settings.width),screen.width),height=min(CGFloat(settings.height),screen.height)
        let frame=NSRect(x:settings.x.map { CGFloat($0) } ?? screen.maxX-width-30,y:settings.y.map { CGFloat($0) } ?? screen.maxY-height-30,width:width,height:height)
        window=CalendarWindow(contentRect:frame,styleMask:.borderless,backing:.buffered,defer:false)
        window.title=appName; window.isReleasedWhenClosed=false; window.isOpaque=false; window.backgroundColor = .clear
        window.hasShadow=false; window.hidesOnDeactivate=false
        window.collectionBehavior=[.canJoinAllSpaces,.stationary,.ignoresCycle]
        window.delegate=self
        canvas=CalendarView(frame:NSRect(origin:.zero,size:frame.size)); canvas.controller=self; canvas.autoresizingMask=[.width,.height]
        window.contentView=canvas
        status=NSStatusBar.system.statusItem(withLength:NSStatusItem.squareLength)
        status.button?.target=self; status.button?.action=#selector(statusClick(_:))
        status.button?.sendAction(on:[.leftMouseUp,.rightMouseUp])
        loadMonth(); setMode(.locked); clampWindow(); saveSettings()
        window.orderFrontRegardless()
        let center=NotificationCenter.default
        for name in [NSNotification.Name.NSCalendarDayChanged,.NSSystemClockDidChange,.NSSystemTimeZoneDidChange] {
            observers.append(center.addObserver(forName:name,object:nil,queue:.main) { [weak self] _ in
                Task { @MainActor [weak self] in self?.checkDate() }
            })
        }
        observers.append(center.addObserver(forName:NSApplication.didChangeScreenParametersNotification,object:nil,queue:.main) { [weak self] _ in
            Task { @MainActor [weak self] in self?.clampWindow() }
        })
        observers.append(NSWorkspace.shared.notificationCenter.addObserver(forName:NSWorkspace.didWakeNotification,object:nil,queue:.main) { [weak self] _ in
            Task { @MainActor [weak self] in self?.checkDate() }
        })
        scheduleMidnight()
    }
    func loadMonth() {
        cells=Day.cells(year:settings.year,month:settings.month).map { day in
            do { return Cell(day:day,note:try store.read(day),holiday:settings.showHolidays ? Holidays.label(day):"",readable:true) }
            catch { saveFailed=true; return Cell(day:day,note:"",holiday:settings.showHolidays ? Holidays.label(day):"",readable:false) }
        }
        canvas?.needsDisplay=true; updateStatus()
    }
    func font(size:CGFloat,numeric:Bool=false)->NSFont {
        if settings.handwritten {
            if let value=NSFont(name:numeric ? "Noteworthy-Light":"Kaiti SC",size:size) { return value }
        }
        return NSFont.systemFont(ofSize:size,weight:.regular)
    }
    func setMode(_ value:Mode) {
        guard finishEditing(save:true) else { return }
        mode=value
        window.ignoresMouseEvents=value == .locked
        window.level=value == .locked ? NSWindow.Level(rawValue:Int(CGWindowLevelForKey(.desktopIconWindow))+1) : .normal
        if value != .locked { NSApp.activate(ignoringOtherApps:true); window.makeKeyAndOrderFront(nil); window.makeFirstResponder(canvas) }
        canvas.needsDisplay=true; updateStatus()
    }
    func finish() { guard finishEditing(save:true) else { return }; saveSettings(); setMode(.locked) }
    func changeMonth(_ year:Int,_ month:Int,follow:Bool=false) {
        guard (1601...9999).contains(year),(1...12).contains(month),finishEditing(save:true) else { return }
        settings.year=year; settings.month=month; settings.followToday=follow; loadMonth(); saveSettings()
    }
    func stepMonth(_ amount:Int) {
        guard finishEditing(save:true) else { return }
        let total=(settings.year-1)*12+settings.month-1+amount
        changeMonth(total/12+1,total%12+1)
    }
    func goToday() { today = .today; changeMonth(today.year,today.month,follow:true) }
    func editorFrame()->NSRect {
        let l=Layout(size:canvas.bounds.size)
        guard let kind=editor?.kind else { return .zero }
        switch kind {
        case .year:return l.year
        case .month:return l.month
        case .fontSize:return l.fontSize
        case .note:guard let i=cells.firstIndex(where:{$0.day==editingDay}) else { return .zero }; return l.note(i)
        }
    }
    func positionEditor() { if let editor=editor { editor.frame=editorFrame(); editor.textContainer?.containerSize=NSSize(width:editor.frame.width,height:.greatestFiniteMagnitude) } }
    func beginEditor(_ kind:EditKind,index:Int?=nil) {
        guard mode != .locked,finishEditing(save:true) else { return }
        if kind == .note { guard let i=index,cells.indices.contains(i),cells[i].readable else { return }; editingDay=cells[i].day }
        originalYear=settings.year; originalMonth=settings.month; originalFollow=settings.followToday
        let field=InlineTextView(frame:.zero); field.controller=self; field.kind=kind
        field.delegate=self; field.isRichText=false; field.importsGraphics=false; field.drawsBackground=false; field.backgroundColor = .clear
        field.textColor = .white; field.insertionPointColor = .white; field.textContainerInset = .zero
        field.textContainer?.lineFragmentPadding=0; field.isHorizontallyResizable=false; field.isVerticallyResizable=false
        field.isAutomaticQuoteSubstitutionEnabled=false; field.isAutomaticDashSubstitutionEnabled=false; field.isAutomaticTextReplacementEnabled=false
        field.isAutomaticSpellingCorrectionEnabled=false; field.allowsUndo=true
        let s=Layout(size:canvas.bounds.size).s
        field.font=font(size:max(8,CGFloat(kind == .note ? settings.noteFontSize : kind == .fontSize ? 14:20)*s),numeric:kind != .note)
        switch kind {
        case .note:field.string=index.map{cells[$0].note} ?? ""
        case .year:field.string=String(settings.year)
        case .month:field.string=String(settings.month)
        case .fontSize:field.string=String(settings.noteFontSize)
        }
        editor=field; positionEditor(); canvas.addSubview(field)
        window.makeFirstResponder(field)
        field.setSelectedRange(kind == .note ? NSRange(location:(field.string as NSString).length,length:0) : NSRange(location:0,length:(field.string as NSString).length))
        canvas.needsDisplay=true
    }
    private func number(_ text:String,kind:EditKind)->Int? {
        guard !text.isEmpty,text.count <= (kind == .year ? 4:2),text.utf8.allSatisfy({$0>=48&&$0<=57}),let n=Int(text) else { return nil }
        if kind == .year { return (1601...9999).contains(n) ? n:nil }
        if kind == .fontSize { return (8...32).contains(n) ? n:nil }
        return (1...12).contains(n) ? n:nil
    }
    func textDidChange(_ notification:Notification) {
        guard !finishing,let field=editor,field.kind == .year || field.kind == .month else { return }
        let value=number(field.string,kind:field.kind)
        let year=field.kind == .year ? value ?? originalYear:originalYear
        let month=field.kind == .month ? value ?? originalMonth:originalMonth
        if settings.year != year || settings.month != month {
            settings.year=year; settings.month=month
            settings.followToday=year==originalYear && month==originalMonth ? originalFollow:false
            loadMonth()
        }
    }
    @discardableResult func finishEditing(save:Bool)->Bool {
        guard !finishing,let field=editor else { return true }
        finishing=true; defer { finishing=false }
        if field.hasMarkedText() { field.unmarkText() }
        let value=field.string
        if field.kind == .note {
            if save,let day=editingDay,value.utf8.count<=1024*1024,!value.contains("\0") {
                do {
                    try store.save(value,for:day)
                    if let i=cells.firstIndex(where:{$0.day==day}) { cells[i].note=value }
                    saveFailed=false
                } catch { saveFailed=true; updateStatus(); canvas.needsDisplay=true; return false }
            }
        } else if field.kind == .fontSize {
            if save,let n=number(value,kind:.fontSize) { settings.noteFontSize=n; saveSettings() }
        } else {
            if save,let n=number(value,kind:field.kind) {
                settings.year=field.kind == .year ? n:originalYear; settings.month=field.kind == .month ? n:originalMonth
                settings.followToday=settings.year==originalYear && settings.month==originalMonth ? originalFollow:false
            } else { settings.year=originalYear; settings.month=originalMonth; settings.followToday=originalFollow }
            loadMonth(); saveSettings()
        }
        field.removeFromSuperview(); editor=nil; editingDay=nil; window.makeFirstResponder(canvas); canvas.needsDisplay=true; updateStatus(); return true
    }
    func textDidEndEditing(_ notification:Notification) { if !finishing { _=finishEditing(save:true) } }
    func windowDidResignKey(_ notification:Notification) { _=finishEditing(save:true) }
    func windowDidResize(_ notification:Notification) { positionEditor(); canvas?.needsDisplay=true }
    func windowDidMove(_ notification:Notification) { if !moving { saveSettings() } }
    func clampWindow() {
        guard let window=window else { return }
        var frame=window.frame
        let screen=NSScreen.screens.first(where:{$0.visibleFrame.intersects(frame)})?.visibleFrame ?? NSScreen.main?.visibleFrame ?? frame
        frame.size.width=min(max(560,frame.width),screen.width); frame.size.height=min(max(420,frame.height),screen.height)
        frame.origin.x=min(max(frame.minX,screen.minX),screen.maxX-frame.width)
        frame.origin.y=min(max(frame.minY,screen.minY),screen.maxY-frame.height)
        window.setFrame(frame,display:true)
    }
    func saveSettings() {
        if let frame=window?.frame { settings.x=Double(frame.minX); settings.y=Double(frame.minY); settings.width=Double(frame.width); settings.height=Double(frame.height) }
        do { try store.saveSettings(settings) } catch { saveFailed=true; updateStatus() }
    }
    private func scheduleMidnight() {
        midnight?.invalidate()
        let calendar=Day.calendar,start=calendar.startOfDay(for:Date())
        guard let next=calendar.date(byAdding:.day,value:1,to:start) else { return }
        midnight=Timer(fire:next.addingTimeInterval(0.2),interval:0,repeats:false) { [weak self] _ in
            Task { @MainActor [weak self] in self?.checkDate() }
        }
        RunLoop.main.add(midnight!,forMode:.common)
    }
    func checkDate() {
        today = .today
        if settings.followToday && (today.year != settings.year || today.month != settings.month) { changeMonth(today.year,today.month,follow:true) }
        else { canvas.needsDisplay=true }
        scheduleMidnight()
    }
    private func updateStatus() {
        guard let status=status else { return }
        let active=mode != .locked
        status.button?.toolTip=saveFailed ? "豆泥日历 · 保存失败" : active ? "豆泥日历 · 编辑模式" : appName
        let image=NSImage(size:NSSize(width:18,height:18),flipped:false) { rect in
            let ink=active ? NSColor(calibratedRed:0.97,green:0.80,blue:0.45,alpha:1):NSColor(calibratedRed:0.50,green:0.81,blue:0.76,alpha:1)
            ink.setFill(); NSBezierPath(roundedRect:NSRect(x:2,y:1,width:14,height:14),xRadius:2,yRadius:2).fill()
            NSColor(calibratedRed:0.10,green:0.19,blue:0.22,alpha:1).setFill()
            NSRect(x:4,y:10,width:10,height:1).fill()
            for y in [4,7] { for x in [5,9,13] { NSBezierPath(ovalIn:NSRect(x:CGFloat(x),y:CGFloat(y),width:1,height:1)).fill() } }
            ink.setStroke(); let rings=NSBezierPath(); rings.lineWidth=2; rings.move(to:NSPoint(x:6,y:13)); rings.line(to:NSPoint(x:6,y:17)); rings.move(to:NSPoint(x:12,y:13)); rings.line(to:NSPoint(x:12,y:17)); rings.stroke()
            return true
        }
        image.isTemplate=false; status.button?.image=image
    }
    @objc func statusClick(_ sender:Any?) {
        if NSApp.currentEvent?.type == .rightMouseUp {
            guard finishEditing(save:true),let button=status.button else { return }
            let menu=NSMenu()
            func item(_ title:String,_ tag:Int,_ checked:Bool=false) {
                let value=NSMenuItem(title:title,action:#selector(menuClick(_:)),keyEquivalent:"")
                value.target=self; value.tag=tag; value.state=checked ? .on:.off; menu.addItem(value)
            }
            item("编辑模式",1,mode == .edit); item("移动与缩放",2,mode == .transform); menu.addItem(.separator())
            item("手绘风格",3,settings.handwritten); item("字体大小",4); item("显示节假日",5,settings.showHolidays); menu.addItem(.separator())
            item("登录时启动",6,SMAppService.mainApp.status == .enabled); item("退出",7)
            menu.popUp(positioning:nil,at:NSPoint(x:0,y:button.bounds.maxY+4),in:button)
        } else { if mode == .edit { finish() } else { setMode(.edit) } }
    }
    @objc func menuClick(_ sender:NSMenuItem) {
        switch sender.tag {
        case 1:if mode == .edit { finish() } else { setMode(.edit) }
        case 2:if mode == .transform { finish() } else { setMode(.transform) }
        case 3:guard finishEditing(save:true) else { return }; settings.handwritten.toggle(); saveSettings(); canvas.needsDisplay=true
        case 4:setMode(.edit); beginEditor(.fontSize)
        case 5:guard finishEditing(save:true) else { return }; settings.showHolidays.toggle(); loadMonth(); saveSettings()
        case 6:
            do { if SMAppService.mainApp.status == .enabled { try SMAppService.mainApp.unregister() } else { try SMAppService.mainApp.register() } }
            catch { saveFailed=true; updateStatus() }
        case 7:NSApp.terminate(nil)
        default:break
        }
    }
    func applicationShouldHandleReopen(_ sender:NSApplication,hasVisibleWindows flag:Bool)->Bool { setMode(.edit); return true }
    func applicationShouldTerminate(_ sender:NSApplication)->NSApplication.TerminateReply {
        guard finishEditing(save:true) else { return .terminateCancel }; saveSettings(); return .terminateNow
    }
    func applicationWillTerminate(_ notification:Notification) { midnight?.invalidate() }
}

enum SelfTests {
    static func run()->Bool {
        var count=0
        func check(_ condition:Bool,_ message:String) {
            count+=1
            if !condition { FileHandle.standardError.write(Data("FAIL: \(message)\n".utf8)); exit(1) }
        }
        check(Day(2024,2,29).valid && !Day(2023,2,29).valid,"Gregorian leap dates")
        for year in 2000..<2400 {
            for month in 1...12 {
                let cells=Day.cells(year:year,month:month)
                check(cells.count==42 && cells.first!.weekday==0,"Monday-first complete month")
                check(cells[0].adding(41)==cells[41],"month spillovers are continuous")
            }
        }
        check(Day.cells(year:2026,month:11).first==Day(2026,10,26),"previous-month spillover")
        check(Day.cells(year:2026,month:11).last==Day(2026,12,6),"next-month spillover")
        check(Holidays.label(Day(2026,2,17)).contains("春节"),"Spring Festival 2026")
        check(Holidays.label(Day(2026,6,19)).contains("端午"),"Dragon Boat 2026")
        check(Holidays.label(Day(2026,9,25)).contains("中秋"),"Mid-Autumn 2026")
        check(Holidays.label(Day(2026,4,5)).contains("清明"),"Qingming 2026")
        let fixture=FileManager.default.temporaryDirectory.appendingPathComponent("douni-tests-"+UUID().uuidString,isDirectory:true)
        defer { try? FileManager.default.removeItem(at:fixture) }
        let store=Store(directory:fixture),day=Day(2026,11,16)
        do {
            try store.save("中文记事 🫘\n第二行",for:day)
            check(try store.read(day)=="中文记事 🫘\n第二行","Unicode note roundtrip")
            try store.save("新版",for:day)
            let backup=try String(contentsOf:store.noteURL(day,backup:true),encoding:.utf8)
            check(backup=="中文记事 🫘\n第二行","previous-version backup")
            var settings=Settings(); settings.handwritten=true; settings.noteFontSize=18
            try store.saveSettings(settings)
            let reopened=Store(directory:fixture),loaded=reopened.loadSettings()
            check(loaded.handwritten && loaded.noteFontSize==18,"settings survive restart")
            settings.noteFontSize=33
            do { try reopened.saveSettings(settings); check(false,"invalid setting rejected") } catch { check(true,"invalid setting rejected") }
            check(reopened.loadSettings().noteFontSize==18,"invalid setting preserves prior value")
            try Data([0xff,0xfe,0xff]).write(to:store.noteURL(day))
            do { try store.save("不得覆盖损坏文件",for:day); check(false,"corrupt notes must reject overwrite") } catch { check(true,"corrupt note protected") }
            let bytes=try Data(contentsOf:store.noteURL(day)); check(bytes==Data([0xff,0xfe,0xff]),"corrupt note bytes preserved")
        } catch { check(false,"storage: \(error)") }
        print("PASS: \(count) macOS native date/storage checks")
        return true
    }
}

if CommandLine.arguments.contains("--self-test") { exit(SelfTests.run() ? 0:1) }
let application=NSApplication.shared
application.setActivationPolicy(.accessory)
let controller=AppController()
application.delegate=controller
application.run()
