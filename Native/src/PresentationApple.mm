#include "PresentationImage_p.h"
#include <mutex>
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#import <QuickLookThumbnailing/QuickLookThumbnailing.h>

namespace iiFileProvider::native::detail {
namespace {
std::optional<FilePreview> encode(CGImageRef image) {
    if (!image || CGImageGetWidth(image) > 512 || CGImageGetHeight(image) > 512) return {};
    NSMutableData *data = [NSMutableData data];
    const auto destination = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, CFSTR("public.png"), 1, nullptr);
    if (!destination) return {};
    CGImageDestinationAddImage(destination, image, nullptr);
    const bool ok = CGImageDestinationFinalize(destination); CFRelease(destination);
    if (!ok || data.length > Presentation::maximumDocumentBytes / 2) return {};
    return FilePreview{"image/png", "image", std::string(static_cast<const char *>(data.bytes), data.length),
        unsigned(CGImageGetWidth(image)), unsigned(CGImageGetHeight(image))};
}
}
std::optional<FilePreview> applePreview(const std::filesystem::path &path, FileDetails &details) {
    @autoreleasepool {
        const auto value = path.u8string();
        NSString *name = [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
        if (!name) return {};
        NSURL *url = [NSURL fileURLWithPath:name];
        if (const auto source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr)) {
            NSDictionary *properties = CFBridgingRelease(CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr));
            NSNumber *width = properties[(__bridge NSString *)kCGImagePropertyPixelWidth];
            NSNumber *height = properties[(__bridge NSString *)kCGImagePropertyPixelHeight];
            if (width && height) {
                details.properties["width"] = std::to_string(width.unsignedLongLongValue);
                details.properties["height"] = std::to_string(height.unsignedLongLongValue);
            }
            const auto add = [&](const char *key, id value) {
                if (!value) return;
                const char *utf8 = [[value description] UTF8String];
                if (utf8) { std::string text(utf8); if (text.size() <= 8192) details.properties[key] = std::move(text); }
            };
            NSDictionary *exif = properties[(__bridge NSString *)kCGImagePropertyExifDictionary];
            NSDictionary *tiff = properties[(__bridge NSString *)kCGImagePropertyTIFFDictionary];
            add("cameraMake", tiff[(__bridge NSString *)kCGImagePropertyTIFFMake]);
            add("cameraModel", tiff[(__bridge NSString *)kCGImagePropertyTIFFModel]);
            add("capturedAt", exif[(__bridge NSString *)kCGImagePropertyExifDateTimeOriginal]);
            add("exposureSeconds", exif[(__bridge NSString *)kCGImagePropertyExifExposureTime]);
            add("fNumber", exif[(__bridge NSString *)kCGImagePropertyExifFNumber]);
            add("iso", exif[(__bridge NSString *)kCGImagePropertyExifISOSpeedRatings]);
            add("orientation", properties[(__bridge NSString *)kCGImagePropertyOrientation]);
            add("colorModel", properties[(__bridge NSString *)kCGImagePropertyColorModel]);
            const auto options = (__bridge CFDictionaryRef)@{
                (__bridge NSString *)kCGImageSourceCreateThumbnailFromImageAlways: @YES,
                (__bridge NSString *)kCGImageSourceCreateThumbnailWithTransform: @YES,
                (__bridge NSString *)kCGImageSourceThumbnailMaxPixelSize: @512,
                (__bridge NSString *)kCGImageSourceShouldCacheImmediately: @NO};
            const auto image = CGImageSourceCreateThumbnailAtIndex(source, 0, options);
            auto preview = encode(image);
            if (image) CGImageRelease(image); CFRelease(source);
            if (preview) return preview;
        }
        // Quick Look also covers native PDF, office and movie providers. This
        // runs on the provider worker, has a bounded wait, and performs no writes
        // outside the provider. Cancellation leaves its captured state alive.
        if (@available(macOS 10.15, iOS 13.0, *)) {
            struct State { std::mutex mutex; std::optional<FilePreview> preview; };
            auto state = std::make_shared<State>();
            dispatch_semaphore_t ready = dispatch_semaphore_create(0);
            QLThumbnailGenerationRequest *request = [[QLThumbnailGenerationRequest alloc]
                initWithFileAtURL:url size:CGSizeMake(512, 512) scale:1
                representationTypes:QLThumbnailGenerationRequestRepresentationTypeThumbnail];
            [QLThumbnailGenerator.sharedGenerator generateBestRepresentationForRequest:request
                completionHandler:^(QLThumbnailRepresentation *representation, NSError *) {
                    { std::lock_guard lock(state->mutex); state->preview = encode(representation.CGImage); }
                    dispatch_semaphore_signal(ready);
                }];
            if (dispatch_semaphore_wait(ready, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC)) != 0) {
                [QLThumbnailGenerator.sharedGenerator cancelRequest:request]; return {};
            }
            std::lock_guard lock(state->mutex);
            return state->preview;
        }
        return {};
    }
}
}
