#pragma once
#include <QJsonObject>
#include <QVariantMap>
#include <cmath>

// Customer projection only; does not grant device authority or calibration.
inline bool applyCustomerDraft(const QJsonObject &active, const QVariantMap &draft, QJsonObject *out)
{
    if (!out || active["schema_version"].toInt() != 2 || active["geometry_model"].toString() != "BODY_REFERENCE_V1") return false;
    const QStringList lengths {"l_total_mm", "l_b_mm", "l_d_mm", "l_e_mm"};
    QStringList allowed=lengths; allowed << "model_name";
    for (auto i=draft.cbegin();i!=draft.cend();++i) if(!allowed.contains(i.key())) return false;
    QJsonObject result=active;
    for(const auto &key:lengths) {
        bool ok=false; const double value=draft.value(key).toDouble(&ok);
        if(!ok || !std::isfinite(value) || value<=0) return false;
        result[key]=value;
    }
    const auto name=draft.value("model_name").toString().trimmed();
    if(name.isEmpty()) return false;
    result["model_name"]=name;
    const double mRef=active["mold_reference_mm"].toDouble(), cRef=active["body_reference_mm"].toDouble();
    const double mold=active["fixed_mold_thickness_mm"].toDouble();
    const double remaining=result["l_total_mm"].toDouble()-result["l_b_mm"].toDouble()-result["l_d_mm"].toDouble()-result["l_e_mm"].toDouble();
    const double currentC=cRef+(mold-mRef);
    if(!std::isfinite(mRef)||!std::isfinite(cRef)||!std::isfinite(mold)||mRef<=0||cRef<=0||mold<=0
        || !std::isfinite(currentC)||currentC<=0||remaining-cRef<=0||remaining-currentC<=0) return false;
    *out=result; return true;
}
