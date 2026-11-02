// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
//
// wincap: record one macOS window and its app's sound with ScreenCaptureKit.
// Used for FS-UAE (classic 68k): it captures the emulator's window only, even
// when it sits in its own full-screen Space, without the rest of the desktop.
//   swiftc -O -o wincap scripts/wincap.swift
//   wincap list [app-or-title substring]           window ids
//   wincap rec <windowID> <out.mov> [secs] [fps] [divisor]   Ctrl-C/SIGINT stops
//   wincap shot <windowID> <out.png>   (failed with SCStream error -3811 on macOS 27; rec works)
// divisor 2 records a Retina window at its point size. Needs Screen Recording
// permission for the terminal.
import Foundation
import AppKit
import ScreenCaptureKit
import AVFoundation
import CoreMedia

func die(_ s: String) -> Never { FileHandle.standardError.write((s + "\n").data(using: .utf8)!); exit(1) }

let args = CommandLine.arguments
if args.count < 2 { die("usage: wincap list|rec|shot ...") }

func content() async -> SCShareableContent {
    do { return try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: false) }
    catch { die("SCShareableContent: \(error)") }
}

final class Rec: NSObject, SCStreamOutput, SCStreamDelegate {
    let writer: AVAssetWriter
    let vin: AVAssetWriterInput
    let ain: AVAssetWriterInput
    var started = false
    var frames = 0
    let q = DispatchQueue(label: "w")
    init(url: URL, w: Int, h: Int) throws {
        try? FileManager.default.removeItem(at: url)
        writer = try AVAssetWriter(outputURL: url, fileType: .mov)
        vin = AVAssetWriterInput(mediaType: .video, outputSettings: [
            AVVideoCodecKey: AVVideoCodecType.h264, AVVideoWidthKey: w, AVVideoHeightKey: h,
            AVVideoCompressionPropertiesKey: [AVVideoAverageBitRateKey: 20_000_000]])
        vin.expectsMediaDataInRealTime = true
        ain = AVAssetWriterInput(mediaType: .audio, outputSettings: [
            AVFormatIDKey: kAudioFormatMPEG4AAC, AVSampleRateKey: 48000, AVNumberOfChannelsKey: 2,
            AVEncoderBitRateKey: 192000])
        ain.expectsMediaDataInRealTime = true
        writer.add(vin); writer.add(ain)
    }
    func stream(_ stream: SCStream, didOutputSampleBuffer sb: CMSampleBuffer, of type: SCStreamOutputType) {
        q.sync {
            guard sb.isValid else { return }
            if type == .screen {
                guard let att = CMSampleBufferGetSampleAttachmentsArray(sb, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
                      let raw = att.first?[.status] as? Int, let st = SCFrameStatus(rawValue: raw), st == .complete else { return }
                if !started {
                    writer.startWriting(); writer.startSession(atSourceTime: sb.presentationTimeStamp); started = true
                }
                if vin.isReadyForMoreMediaData { vin.append(sb); frames += 1 }
            } else if type == .audio, started {
                if ain.isReadyForMoreMediaData { ain.append(sb) }
            }
        }
    }
    func stream(_ stream: SCStream, didStopWithError error: Error) { FileHandle.standardError.write("stream stopped: \(error)\n".data(using: .utf8)!) }
    func finish() async {
        q.sync { vin.markAsFinished(); ain.markAsFinished() }
        await writer.finishWriting()
    }
}

_ = NSApplication.shared
NSApp.setActivationPolicy(.prohibited)
Task {
    let c = await content()
    switch args[1] {
    case "list":
        let f = args.count > 2 ? args[2].lowercased() : ""
        for w in c.windows where f.isEmpty || (w.owningApplication?.applicationName.lowercased().contains(f) ?? false) || (w.title?.lowercased().contains(f) ?? false) {
            print(w.windowID, w.owningApplication?.processID ?? 0, w.owningApplication?.applicationName ?? "?", "|", w.title ?? "", "|", Int(w.frame.width), "x", Int(w.frame.height), w.isOnScreen ? "onscreen" : "offscreen", "layer", w.windowLayer)
        }
        exit(0)
    case "shot", "rec":
        guard args.count >= 4, let wid = UInt32(args[2]), let win = c.windows.first(where: { $0.windowID == wid }) else { die("no such window") }
        let filter = SCContentFilter(desktopIndependentWindow: win)
        let cfg = SCStreamConfiguration()
        let scale = Int(filter.pointPixelScale)
        let div = args.count > 6 ? Int(args[6])! : 1
        let fw = Int(win.frame.width) * scale / div / 2 * 2, fh = Int(win.frame.height) * scale / div / 2 * 2
        cfg.width = fw; cfg.height = fh
        cfg.showsCursor = false
        cfg.queueDepth = 8
        if args[1] == "shot" {
            do {
                let img = try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: cfg)
                let url = URL(fileURLWithPath: args[3]) as CFURL
                let dest = CGImageDestinationCreateWithURL(url, "public.png" as CFString, 1, nil)!
                CGImageDestinationAddImage(dest, img, nil); CGImageDestinationFinalize(dest)
                print("ok \(img.width)x\(img.height)")
            } catch { die("shot: \(error)") }
            exit(0)
        }
        let secs = args.count > 4 ? Double(args[4])! : 3600
        let fps = args.count > 5 ? Int32(args[5])! : 30
        cfg.minimumFrameInterval = CMTime(value: 1, timescale: fps)
        cfg.capturesAudio = true
        cfg.sampleRate = 48000; cfg.channelCount = 2
        cfg.excludesCurrentProcessAudio = true
        let rec: Rec
        do { rec = try Rec(url: URL(fileURLWithPath: args[3]), w: fw, h: fh) } catch { die("writer: \(error)") }
        let stream = SCStream(filter: filter, configuration: cfg, delegate: rec)
        do {
            try stream.addStreamOutput(rec, type: .screen, sampleHandlerQueue: DispatchQueue(label: "v"))
            try stream.addStreamOutput(rec, type: .audio, sampleHandlerQueue: DispatchQueue(label: "a"))
            try await stream.startCapture()
        } catch { die("start: \(error)") }
        print("recording \(fw)x\(fh)"); fflush(stdout)
        var stop = false
        for s in [SIGINT, SIGTERM] {
            signal(s, SIG_IGN)
            let src = DispatchSource.makeSignalSource(signal: s, queue: .main)
            src.setEventHandler { stop = true }
            src.resume()
            _ = Unmanaged.passRetained(src)
        }
        let t0 = Date()
        while !stop && Date().timeIntervalSince(t0) < secs { try? await Task.sleep(nanoseconds: 50_000_000) }
        try? await stream.stopCapture()
        await rec.finish()
        print("done frames=\(rec.frames) secs=\(Date().timeIntervalSince(t0))")
        exit(0)
    default: die("bad cmd")
    }
}
dispatchMain()
