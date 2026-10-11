#include "compat/library-path.hpp"
#include "ftnoir_tracker_neuralnet.h"
#include <QLibrary>
#include <onnxruntime_cxx_api.h>

namespace neuralnet_tracker_ns
{

bool NeuralNetTracker::maybe_load_onnxruntime_dynamically()
{
#if defined ORT_API_MANUAL_INIT
    QLibrary lib;
    lib.setLoadHints(QLibrary::PreventUnloadHint);
    lib.setFileName(
        OPENTRACK_BASE_PATH + OPENTRACK_LIBRARY_PATH
        "onnxruntime" "." OPENTRACK_LIBRARY_EXTENSION);
    qDebug() << "tracker/nn: loading onnxruntime library" << lib.fileName();
    if (!lib.load())
    {
        qDebug().nospace() << "tracker/nn: can't load onnxruntime library "
                           << ": " << lib.errorString();
        return false;
    }

    auto* fn_OrtGetApiBase = (void*)lib.resolve("OrtGetApiBase");
    if (!fn_OrtGetApiBase)
    {
        qDebug() << "tracker/nn: missing OrtGetApiBase:" << lib.errorString();
        return false;
    }
    using OrtGetApiBase_t = const OrtApiBase*(ORT_API_CALL*)(void);
    const auto* ort_base = reinterpret_cast<OrtGetApiBase_t>(fn_OrtGetApiBase)();
    if (!ort_base)
    {
        qDebug() << "tracker/nn: invalid ONNX Runtime API base";
        return false;
    }
    const auto* ort_api = ort_base->GetApi(ORT_API_VERSION);
    if (!ort_api)
    {
        qDebug() << "tracker/nn: incompatible ONNX Runtime API version";
        return false;
    }

    Ort::InitApi(ort_api);
#endif
    return true;
}

} // namespace neuralnet_tracker_ns
