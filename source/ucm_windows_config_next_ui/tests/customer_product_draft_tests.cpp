#include "customer_product_draft.h"
#include <QJsonArray>
#include <QCoreApplication>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QJsonObject active{{"schema_version",2},{"geometry_model","BODY_REFERENCE_V1"},
        {"mold_reference_mm",1075},{"body_reference_mm",2000},{"fixed_mold_thickness_mm",1075},
        {"rod_diameter_mm",QJsonArray{90,90,90,90}},{"abeq_mm2",5000},{"phi_b",0.46}};
    QVariantMap draft{{"model_name","DE168"},{"l_total_mm",2500},{"l_b_mm",90},{"l_d_mm",90},{"l_e_mm",40}};
    QJsonObject out;
    if(!applyCustomerDraft(active,draft,&out)||out["abeq_mm2"]!=active["abeq_mm2"]||out["phi_b"]!=active["phi_b"]) return 1;
    draft["verified"]=true; if(applyCustomerDraft(active,draft,&out))return 2; draft.remove("verified");
    draft["phi_b"]=0.7; if(applyCustomerDraft(active,draft,&out))return 3; draft.remove("phi_b");
    draft["rod_diameter_mm"]=91; if(applyCustomerDraft(active,draft,&out))return 4;
    draft["rod_diameter_mm"]=90; if(applyCustomerDraft(active,draft,&out))return 5; draft.remove("rod_diameter_mm");
    draft["l_total_mm"]=2200; if(applyCustomerDraft(active,draft,&out))return 6; draft["l_total_mm"]=2500;
    active["fixed_mold_thickness_mm"]=1400; if(applyCustomerDraft(active,draft,&out))return 7; active["fixed_mold_thickness_mm"]=1075;
    active["schema_version"]=1; if(applyCustomerDraft(active,draft,&out))return 8;
    return 0;
}
