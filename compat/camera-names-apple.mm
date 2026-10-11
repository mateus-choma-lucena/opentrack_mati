/* macOS camera enumeration via AVFoundation.
 *
 * Why this file exists
 * --------------------
 * opentrack's generic `get_camera_names()` previously used
 * QMediaDevices::videoInputs() to populate the camera dropdown on
 * macOS. That was convenient but cross-wired: the OpenCV VideoCapture
 * backend that actually opens the camera is CAP_AVFOUNDATION, whose
 * device-index ordering does not match QMediaDevices' ordering.
 * After a USB hot-plug the two lists can disagree by one, so the user
 * picks "CREALITY CAM" in the dropdown, the ini saves it, the lookup
 * returns QMediaDevices' index for CREALITY (say 0), OpenCV opens
 * VideoCapture(0) which - in AVFoundation's order - is the lid
 * camera. Result: "I picked X and it used Y".
 *
 * Enumerating with AVFoundation here matches exactly what OpenCV's
 * CAP_AVFOUNDATION sees (same API, same call path), so the index we
 * return is the one OpenCV will use.
 *
 * Called from the __APPLE__ branch in compat/camera-names.cpp.
 */
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#include <QString>
#include <vector>
#include <tuple>

namespace compat_apple {

// OpenCV's CAP_AVFOUNDATION (cap_avfoundation_mac.mm) enumerates as:
//
//     NSArray* devices =
//         [[AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo]
//          arrayByAddingObjectsFromArray:
//             [AVCaptureDevice devicesWithMediaType:AVMediaTypeMuxed]];
//     devices = [devices sortedArrayUsingComparator:
//         ^NSComparisonResult(AVCaptureDevice *d1, AVCaptureDevice *d2) {
//             return [d1.uniqueID compare:d2.uniqueID];
//         }];
//
// Note the SORT BY uniqueID - that's the subtlety that broke my first
// attempt. devicesWithMediaType: returns cameras in native AVFoundation
// order (typically built-in first, then external-by-connection-time),
// but OpenCV then reorders them alphabetically by uniqueID string
// before indexing into the array. So a MacBook cam with uniqueID
// "6C707041-..." ends up AFTER a Creality webcam with uniqueID
// "0x1000001d6c0103" - because '0' < '6' lexicographically.
//
// We must replicate the full pipeline (video+muxed concat, then
// uniqueID sort) so the indices we return match what OpenCV's
// VideoCapture(idx, CAP_AVFOUNDATION) will open.
std::vector<std::tuple<QString, int>> get_camera_names_apple()
{
    std::vector<std::tuple<QString, int>> ret;
    @autoreleasepool {
        NSArray<AVCaptureDevice*>* video =
            [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
        NSArray<AVCaptureDevice*>* muxed =
            [AVCaptureDevice devicesWithMediaType:AVMediaTypeMuxed];
        NSArray<AVCaptureDevice*>* devices =
            [video arrayByAddingObjectsFromArray:muxed];
        devices = [devices sortedArrayUsingComparator:
            ^NSComparisonResult(AVCaptureDevice* d1, AVCaptureDevice* d2) {
                return [d1.uniqueID compare:d2.uniqueID];
            }];
        int idx = 0;
        for (AVCaptureDevice* d in devices) {
            QString name = QString::fromNSString(d.localizedName);
            ret.emplace_back(name, idx++);
        }
    }
    return ret;
}

} // namespace compat_apple
