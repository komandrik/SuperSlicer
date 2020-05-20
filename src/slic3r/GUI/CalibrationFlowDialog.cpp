#include "CalibrationFlowDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "GUI_ObjectList.hpp"
#include "Tab.hpp"
#include <wx/scrolwin.h>
#include <wx/display.h>
#include <wx/file.h>
#include "wxExtensions.hpp"

#if ENABLE_SCROLLABLE
static wxSize get_screen_size(wxWindow* window)
{
    const auto idx = wxDisplay::GetFromWindow(window);
    wxDisplay display(idx != wxNOT_FOUND ? idx : 0u);
    return display.GetClientArea().GetSize();
}
#endif // ENABLE_SCROLLABLE

namespace Slic3r {
namespace GUI {

    CalibrationFlowDialog::CalibrationFlowDialog(GUI_App* app, MainFrame* mainframe)
        : DPIDialog(NULL, wxID_ANY, wxString(SLIC3R_APP_NAME) + " - " + _(L("Flow calibration - test objects generation")),
#if ENABLE_SCROLLABLE
        wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
#else
        wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
#endif // ENABLE_SCROLLABLE
{
    this->gui_app = app;
    this->main_frame = mainframe;
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    // fonts
    const wxFont& font = wxGetApp().normal_font();
    const wxFont& bold_font = wxGetApp().bold_font();
    SetFont(font);

    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    //html
    html_viewer = new wxHtmlWindow(this, wxID_ANY,
        wxDefaultPosition, wxSize(500, 500), wxHW_SCROLLBAR_AUTO);
    html_viewer->SetMinSize(wxSize(900, 500));
    html_viewer->LoadPage(Slic3r::resources_dir()+"/calibration/filament_flow/filament_flow.html");
    main_sizer->Add(html_viewer, 1, wxEXPAND | wxALL, 5);

    wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
    wxButton* bt = new wxButton(this, wxID_FILE1, _(L("Generate 10% intervals")));
    bt->Bind(wxEVT_BUTTON, &CalibrationFlowDialog::create_geometry_10, this);
    buttons->Add(bt);
    bt = new wxButton(this, wxID_FILE2, _(L("Generate 2.5% intervals")));
    bt->Bind(wxEVT_BUTTON, &CalibrationFlowDialog::create_geometry_2_5, this);
    buttons->Add(bt);
    wxButton* close = new wxButton(this, wxID_CLOSE, _(L("Close")));
    close->Bind(wxEVT_BUTTON, &CalibrationFlowDialog::closeMe, this);
    buttons->AddButton(close);
    close->SetDefault();
    close->SetFocus();
    SetAffirmativeId(wxID_CLOSE);
    buttons->Realize();
    main_sizer->Add(buttons, 0, wxEXPAND | wxALL, 5);

    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    this->SetEscapeId(wxCLOSE);
    this->SetAffirmativeId(wxCLOSE);
}

void CalibrationFlowDialog::closeMe(wxCommandEvent& event_args) {
    this->Destroy();
}

void CalibrationFlowDialog::create_geometry_10(wxCommandEvent& event_args) {
    create_geometry(80.f, 10.f);
}

void CalibrationFlowDialog::create_geometry_2_5(wxCommandEvent& event_args) {
    create_geometry(92.f, 2.F);
}

void CalibrationFlowDialog::add_part(ModelObject* model_object, std::string input_file, Vec3d move, Vec3d scale) {
    Model model;
    try {
        model = Model::read_from_file(input_file);
    }
    catch (std::exception & e) {
        auto msg = _(L("Error!")) + " " + input_file + " : " + e.what() + ".";
        show_error(this, msg);
        exit(1);
    }

    for (ModelObject *object : model.objects) {
        Vec3d delta = Vec3d::Zero();
        if (model_object->origin_translation != Vec3d::Zero())
        {
            object->center_around_origin();
            delta = model_object->origin_translation - object->origin_translation;
        }
        for (ModelVolume *volume : object->volumes) {
            volume->translate(delta + move);
            if (scale != Vec3d{ 1,1,1 })
                volume->scale(scale);
            ModelVolume* new_volume = model_object->add_volume(*volume);
            new_volume->set_type(ModelVolumeType::MODEL_PART);
            new_volume->name = boost::filesystem::path(input_file).filename().string();

            //volumes_info.push_back(std::make_pair(from_u8(new_volume->name), new_volume->get_mesh_errors_count() > 0));

            // set a default extruder value, since user can't add it manually
            new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));

            //move to bed
           /* const TriangleMesh& hull = new_volume->get_convex_hull();
            float min_z = std::numeric_limits<float>::max();
            for (const stl_facet& facet : hull.stl.facet_start) {
                for (int i = 0; i < 3; ++i)
                    min_z = std::min(min_z, Vec3f::UnitZ().dot(facet.vertex[i]));
            }
            volume->translate(Vec3d(0,0,-min_z));*/
        }
    }
}

void CalibrationFlowDialog::create_geometry(float start, float delta) {
    Plater* plat = this->main_frame->plater();
    Model& model = plat->model();
    plat->reset();
    std::vector<size_t> objs_idx = plat->load_files(std::vector<std::string>{
            Slic3r::resources_dir()+"/calibration/filament_flow/filament_flow_test_cube.amf",
            Slic3r::resources_dir()+"/calibration/filament_flow/filament_flow_test_cube.amf",
            Slic3r::resources_dir()+"/calibration/filament_flow/filament_flow_test_cube.amf",
            Slic3r::resources_dir()+"/calibration/filament_flow/filament_flow_test_cube.amf",
            Slic3r::resources_dir()+"/calibration/filament_flow/filament_flow_test_cube.amf"}, true, false);


    assert(objs_idx.size() == 5);
    const DynamicPrintConfig* printConfig = this->gui_app->get_tab(Preset::TYPE_PRINT)->get_config();
    const DynamicPrintConfig* printerConfig = this->gui_app->get_tab(Preset::TYPE_PRINTER)->get_config();
    
    /// --- scale ---
    // model is created for a 0.4 nozzle, scale xy with nozzle size.
    const ConfigOptionFloats* nozzle_diameter = printerConfig->option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter->values.size() > 0);
    float xyScale = nozzle_diameter->values[0] / 0.4;
    //scale z to have 6 layers
    const ConfigOptionFloatOrPercent* first_layer_height = printConfig->option<ConfigOptionFloatOrPercent>("first_layer_height");
    const ConfigOptionFloat* layer_height = printConfig->option<ConfigOptionFloat>("layer_height");
    float zscale = first_layer_height->get_abs_value(nozzle_diameter->values[0]) + 5 * layer_height->value;
    zscale *= (1 + 0.3 * (10. / delta));
    //do scaling
    if (xyScale < 0.9 || 1.2 < xyScale) {
        for (size_t i = 0; i < 5; i++)
            model.objects[objs_idx[i]]->scale(xyScale, xyScale, zscale);
    } else {
        for (size_t i = 0; i < 5; i++)
            model.objects[objs_idx[i]]->scale(1, 1, zscale);
    }

    //add sub-part after scale
    float zshift = (1 - zscale) / 2;
    if (delta == 10.f && start == 80.f) {
        add_part(model.objects[objs_idx[0]], Slic3r::resources_dir()+"/calibration/filament_flow/m20.amf", Vec3d{ 9,0,zshift });
        add_part(model.objects[objs_idx[1]], Slic3r::resources_dir()+"/calibration/filament_flow/m10.amf", Vec3d{ 9,0,zshift });
        add_part(model.objects[objs_idx[2]], Slic3r::resources_dir()+"/calibration/filament_flow/_0.amf", Vec3d{ 9,0,zshift });
        add_part(model.objects[objs_idx[3]], Slic3r::resources_dir()+"/calibration/filament_flow/p10.amf", Vec3d{ 9,0,zshift });
        add_part(model.objects[objs_idx[4]], Slic3r::resources_dir()+"/calibration/filament_flow/p20.amf", Vec3d{ 9,0,zshift });
    } else if (delta == 2.f && start == 92.f) {
        add_part(model.objects[objs_idx[0]], Slic3r::resources_dir()+"/calibration/filament_flow/m8.amf", Vec3d{ 9,0,zshift });
        add_part(model.objects[objs_idx[1]], Slic3r::resources_dir()+"/calibration/filament_flow/m6.amf", Vec3d{ 9,0,zshift });
        add_part(model.objects[objs_idx[2]], Slic3r::resources_dir()+"/calibration/filament_flow/m4.amf", Vec3d{ 9,0,zshift });
        add_part(model.objects[objs_idx[3]], Slic3r::resources_dir()+"/calibration/filament_flow/m2.amf", Vec3d{ 9,0,zshift });
        add_part(model.objects[objs_idx[4]], Slic3r::resources_dir()+"/calibration/filament_flow/_0.amf", Vec3d{ 9,0,zshift });
    }
    for (size_t i = 0; i < 5; i++) {
        add_part(model.objects[objs_idx[i]], Slic3r::resources_dir()+"/calibration/filament_flow/O.amf", Vec3d{ 0,0,zscale/2.f + 0.5 }, Vec3d{1,1,xyScale });
    }

    /// --- translate ---;
    const ConfigOptionFloat* extruder_clearance_radius = printConfig->option<ConfigOptionFloat>("extruder_clearance_radius");
    const ConfigOptionPoints* bed_shape = printerConfig->option<ConfigOptionPoints>("bed_shape");
    Vec2d bed_size = BoundingBoxf(bed_shape->values).size();
    Vec2d bed_min = BoundingBoxf(bed_shape->values).min;
    float offsetx = 5 + extruder_clearance_radius->value + 10 * xyScale;
    float offsety = 5 + extruder_clearance_radius->value + 10 * xyScale;
    model.objects[objs_idx[0]]->translate({ bed_min.x() + bed_size.x() / 2 - offsetx, bed_min.y() + bed_size.y() / 2 - offsety * 1.5, 0 });
    model.objects[objs_idx[1]]->translate({ bed_min.x() + bed_size.x() / 2 - offsetx, bed_min.y() + bed_size.y() / 2                , 0 });
    model.objects[objs_idx[2]]->translate({ bed_min.x() + bed_size.x() / 2 - offsetx, bed_min.y() + bed_size.y() / 2 + offsety * 1.5, 0 });
    model.objects[objs_idx[3]]->translate({ bed_min.x() + bed_size.x() / 2 + offsetx, bed_min.y() + bed_size.y() / 2 - offsety      , 0 });
    model.objects[objs_idx[4]]->translate({ bed_min.x() + bed_size.x() / 2 + offsetx, bed_min.y() + bed_size.y() / 2 + offsety      , 0 });
    //TODO: if not enough space, forget about complete_objects


    /// --- main config, please modify object config when possible ---
    DynamicPrintConfig new_print_config = *printConfig; //make a copy
    new_print_config.set_key_value("complete_objects", new ConfigOptionBool(true));

    /// --- custom config ---
    for (size_t i = 0; i < 5; i++) {
        model.objects[objs_idx[i]]->config.set_key_value("brim_width", new ConfigOptionFloat(nozzle_diameter->values[0] * 3));
        model.objects[objs_idx[i]]->config.set_key_value("brim_ears", new ConfigOptionBool(false));
        model.objects[objs_idx[i]]->config.set_key_value("perimeters", new ConfigOptionInt(3));
        model.objects[objs_idx[i]]->config.set_key_value("only_one_perimeter_top", new ConfigOptionBool(true));
        model.objects[objs_idx[i]]->config.set_key_value("enforce_full_fill_volume", new ConfigOptionBool(true));
        model.objects[objs_idx[i]]->config.set_key_value("bottom_solid_layers", new ConfigOptionInt(5));
        model.objects[objs_idx[i]]->config.set_key_value("top_solid_layers", new ConfigOptionInt(100));
        model.objects[objs_idx[i]]->config.set_key_value("thin_walls", new ConfigOptionBool(true));
        model.objects[objs_idx[i]]->config.set_key_value("thin_walls_min_width", new ConfigOptionFloatOrPercent(50,true));
        model.objects[objs_idx[i]]->config.set_key_value("gap_fill", new ConfigOptionBool(true)); 
        model.objects[objs_idx[i]]->config.set_key_value("layer_height", new ConfigOptionFloat(nozzle_diameter->values[0]/2));
        model.objects[objs_idx[i]]->config.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(nozzle_diameter->values[0] / 2, false));
        model.objects[objs_idx[i]]->config.set_key_value("external_infill_margin", new ConfigOptionFloatOrPercent(100, true));
        model.objects[objs_idx[i]]->config.set_key_value("solid_fill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinearWGapFill));
        model.objects[objs_idx[i]]->config.set_key_value("top_fill_pattern", new ConfigOptionEnum<InfillPattern>(ipSmooth));
        //set extrusion mult : 80 90 100 110 120
        model.objects[objs_idx[i]]->config.set_key_value("print_extrusion_multiplier", new ConfigOptionPercent(start + (float)i * delta));
    }

    //update plater
    this->gui_app->get_tab(Preset::TYPE_PRINT)->load_config(new_print_config);
    plat->on_config_change(new_print_config);
    plat->changed_objects(objs_idx);
    this->gui_app->get_tab(Preset::TYPE_PRINT)->update_dirty();
    //update everything, easier to code.
    ObjectList* obj = this->gui_app->obj_list();
    obj->update_after_undo_redo();


    plat->reslice();
    plat->select_view_3D("Preview");
}

void CalibrationFlowDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    msw_buttons_rescale(this, em_unit(), { wxID_OK });

    Layout();
    Fit();
    Refresh();
}

wxPanel* CalibrationFlowDialog::create_header(wxWindow* parent, const wxFont& bold_font)
{
    wxPanel* panel = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

    wxFont header_font = bold_font;
#ifdef __WXOSX__
    header_font.SetPointSize(14);
#else
    header_font.SetPointSize(bold_font.GetPointSize() + 2);
#endif // __WXOSX__

    sizer->AddStretchSpacer();

    // text
    wxStaticText* text = new wxStaticText(panel, wxID_ANY, _(L("Keyboard shortcuts")));
    text->SetFont(header_font);
    sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL);

    sizer->AddStretchSpacer();

    panel->SetSizer(sizer);
    return panel;
}

} // namespace GUI
} // namespace Slic3r