/*
 * KiRouter - a push-and-(sometimes-)shove PCB router
 *
 * Copyright (C) 2013-2017 CERN
 * Copyright (C) 2017-2020 KiCad Developers, see AUTHORS.txt for contributors.
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <wx/hyperlink.h>
#include <functional>
using namespace std::placeholders;
#include <board.h>
#include <board_item.h>
#include <footprint.h>
#include <fp_shape.h>
#include <pad.h>
#include <pcb_edit_frame.h>
#include <pcbnew_id.h>
#include <dialogs/dialog_pns_settings.h>
#include <dialogs/dialog_pns_diff_pair_dimensions.h>
#include <dialogs/dialog_track_via_size.h>
#include <widgets/infobar.h>
#include <confirm.h>
#include <bitmaps.h>
#include <tool/action_menu.h>
#include <tool/tool_manager.h>
#include <tool/tool_menu.h>
#include <tools/pcb_actions.h>
#include <tools/selection_tool.h>
#include <tools/grid_helper.h>

#include "router_tool.h"
#include "pns_segment.h"
#include "pns_router.h"
#include "pns_itemset.h"
#include "pns_logger.h"

#include "pns_kicad_iface.h"

#ifdef DEBUG
#include <plugins/kicad/kicad_plugin.h>
#endif

using namespace KIGFX;

/**
 * Flags used by via tool actions
 */
enum VIA_ACTION_FLAGS
{
    // Via type
    VIA_MASK     = 0x03,
    VIA          = 0x00,     ///> Normal via
    BLIND_VIA    = 0x01,     ///> blind/buried via
    MICROVIA     = 0x02,     ///> Microvia

    // Select layer
    SELECT_LAYER = VIA_MASK + 1,    ///> Ask user to select layer before adding via
};


// Actions, being statically-defined, require specialized I18N handling.  We continue to
// use the _() macro so that string harvesting by the I18N framework doesn't have to be
// specialized, but we don't translate on initialization and instead do it in the getters.

#undef _
#define _(s) s

static const TOOL_ACTION ACT_UndoLastSegment( "pcbnew.InteractiveRouter.UndoLastSegment",
        AS_CONTEXT,
        WXK_BACK, "",
        _( "Undo last segment" ),  _( "Stops laying the current track." ),
        checked_ok_xpm );

static const TOOL_ACTION ACT_EndTrack( "pcbnew.InteractiveRouter.EndTrack",
        AS_CONTEXT,
        WXK_END, "",
        _( "Finish Track" ),  _( "Stops laying the current track." ),
        checked_ok_xpm );

static const TOOL_ACTION ACT_AutoEndRoute( "pcbnew.InteractiveRouter.AutoEndRoute",
        AS_CONTEXT,
        'F', "",
        _( "Auto-finish Track" ),  _( "Automagically finishes laying the current track." ) );

static const TOOL_ACTION ACT_PlaceThroughVia( "pcbnew.InteractiveRouter.PlaceVia",
        AS_CONTEXT,
        'V', LEGACY_HK_NAME( "Add Through Via" ),
        _( "Place Through Via" ),
        _( "Adds a through-hole via at the end of currently routed track." ),
        via_xpm, AF_NONE, (void*) VIA_ACTION_FLAGS::VIA );

static const TOOL_ACTION ACT_PlaceBlindVia( "pcbnew.InteractiveRouter.PlaceBlindVia",
        AS_CONTEXT,
        MD_ALT + MD_SHIFT + 'V', LEGACY_HK_NAME( "Add Blind/Buried Via" ),
        _( "Place Blind/Buried Via" ),
        _( "Adds a blind or buried via at the end of currently routed track."),
        via_buried_xpm, AF_NONE, (void*) VIA_ACTION_FLAGS::BLIND_VIA );

static const TOOL_ACTION ACT_PlaceMicroVia( "pcbnew.InteractiveRouter.PlaceMicroVia",
        AS_CONTEXT,
        MD_CTRL + 'V', LEGACY_HK_NAME( "Add MicroVia" ),
        _( "Place Microvia" ), _( "Adds a microvia at the end of currently routed track." ),
        via_microvia_xpm, AF_NONE, (void*) VIA_ACTION_FLAGS::MICROVIA );

static const TOOL_ACTION ACT_SelLayerAndPlaceThroughVia( "pcbnew.InteractiveRouter.SelLayerAndPlaceVia",
        AS_CONTEXT,
        '<', LEGACY_HK_NAME( "Select Layer and Add Through Via" ),
        _( "Select Layer and Place Through Via..." ),
        _( "Select a layer, then add a through-hole via at the end of currently routed track." ),
        select_w_layer_xpm, AF_NONE,
        (void*) ( VIA_ACTION_FLAGS::VIA | VIA_ACTION_FLAGS::SELECT_LAYER ) );

static const TOOL_ACTION ACT_SelLayerAndPlaceBlindVia( "pcbnew.InteractiveRouter.SelLayerAndPlaceBlindVia",
        AS_CONTEXT,
        MD_ALT + '<', LEGACY_HK_NAME( "Select Layer and Add Blind/Buried Via" ),
        _( "Select Layer and Place Blind/Buried Via..." ),
        _( "Select a layer, then add a blind or buried via at the end of currently routed track."),
        select_w_layer_xpm, AF_NONE,
        (void*) ( VIA_ACTION_FLAGS::BLIND_VIA | VIA_ACTION_FLAGS::SELECT_LAYER ) );

static const TOOL_ACTION ACT_CustomTrackWidth( "pcbnew.InteractiveRouter.CustomTrackViaSize",
        AS_CONTEXT,
        'Q', LEGACY_HK_NAME( "Custom Track/Via Size" ),
        _( "Custom Track/Via Size..." ),
        _( "Shows a dialog for changing the track width and via size." ),
        width_track_xpm );

static const TOOL_ACTION ACT_SwitchPosture( "pcbnew.InteractiveRouter.SwitchPosture",
        AS_CONTEXT,
        '/', LEGACY_HK_NAME( "Switch Track Posture" ),
        _( "Switch Track Posture" ),
        _( "Switches posture of the currently routed track." ),
        change_entry_orient_xpm );

static const TOOL_ACTION ACT_SwitchRounding( "pcbnew.InteractiveRouter.SwitchRounding",
        AS_CONTEXT,
        0, LEGACY_HK_NAME( "Switch Corner Rounding" ),
        _( "Switch Corner Rounding" ),
        _( "Switches the corner type of the currently routed track." ),
        switch_corner_rounding_shape_xpm );

#undef _
#define _(s) wxGetTranslation((s))


ROUTER_TOOL::ROUTER_TOOL() :
    TOOL_BASE( "pcbnew.InteractiveRouter" )
{
}


class TRACK_WIDTH_MENU : public ACTION_MENU
{
public:
    TRACK_WIDTH_MENU( PCB_EDIT_FRAME& aFrame ) :
        ACTION_MENU( true ),
        m_frame( aFrame )
    {
        SetIcon( width_track_via_xpm );
        SetTitle( _( "Select Track/Via Width" ) );
    }

protected:
    ACTION_MENU* create() const override
    {
        return new TRACK_WIDTH_MENU( m_frame );
    }

    void update() override
    {
        EDA_UNITS              units = m_frame.GetUserUnits();
        BOARD_DESIGN_SETTINGS& bds = m_frame.GetBoard()->GetDesignSettings();
        bool                   useIndex = !bds.m_UseConnectedTrackWidth &&
                                          !bds.UseCustomTrackViaSize();
        wxString               msg;

        Clear();

        Append( ID_POPUP_PCB_SELECT_AUTO_WIDTH, _( "Use Starting Track Width" ),
                _( "Route using the width of the starting track." ), wxITEM_CHECK );
        Check( ID_POPUP_PCB_SELECT_AUTO_WIDTH,
               bds.m_UseConnectedTrackWidth && !bds.UseCustomTrackViaSize() );

        Append( ID_POPUP_PCB_SELECT_USE_NETCLASS_VALUES, _( "Use Net Class Values" ),
                _( "Use track and via sizes from the net class" ), wxITEM_CHECK );
        Check( ID_POPUP_PCB_SELECT_USE_NETCLASS_VALUES,
               useIndex && bds.GetTrackWidthIndex() == 0 && bds.GetViaSizeIndex() == 0 );

        Append( ID_POPUP_PCB_SELECT_CUSTOM_WIDTH, _( "Use Custom Values..." ),
                _( "Specify custom track and via sizes" ), wxITEM_CHECK );
        Check( ID_POPUP_PCB_SELECT_CUSTOM_WIDTH, bds.UseCustomTrackViaSize() );

        AppendSeparator();

        // Append the list of tracks & via sizes
        for( unsigned i = 0; i < bds.m_TrackWidthList.size(); i++ )
        {
            int width = bds.m_TrackWidthList[i];

            if( i == 0 )
                msg = _( "Track netclass width" );
            else
                msg.Printf( _( "Track %s" ), MessageTextFromValue( units, width ) );

            int menuIdx = ID_POPUP_PCB_SELECT_WIDTH1 + i;
            Append( menuIdx, msg, wxEmptyString, wxITEM_CHECK );
            Check( menuIdx, useIndex && bds.GetTrackWidthIndex() == i );
        }

        AppendSeparator();

        for( unsigned i = 0; i < bds.m_ViasDimensionsList.size(); i++ )
        {
            VIA_DIMENSION via = bds.m_ViasDimensionsList[i];

            if( i == 0 )
                msg = _( "Via netclass values" );
            else
            {
                if( via.m_Drill > 0 )
                    msg.Printf( _("Via %s, drill %s" ),
                                MessageTextFromValue( units, via.m_Diameter ),
                                MessageTextFromValue( units, via.m_Drill ) );
                else
                    msg.Printf( _( "Via %s" ), MessageTextFromValue( units, via.m_Diameter ) );
            }

            int menuIdx = ID_POPUP_PCB_SELECT_VIASIZE1 + i;
            Append( menuIdx, msg, wxEmptyString, wxITEM_CHECK );
            Check( menuIdx, useIndex && bds.GetViaSizeIndex() == i );
        }
    }

    OPT_TOOL_EVENT eventHandler( const wxMenuEvent& aEvent ) override
    {
        BOARD_DESIGN_SETTINGS &bds = m_frame.GetBoard()->GetDesignSettings();
        int id = aEvent.GetId();

        // On Windows, this handler can be called with an event ID not existing in any
        // menuitem, so only set flags when we have an ID match.

        if( id == ID_POPUP_PCB_SELECT_CUSTOM_WIDTH )
        {
            bds.UseCustomTrackViaSize( true );
            bds.m_UseConnectedTrackWidth = false;
            m_frame.GetToolManager()->RunAction( ACT_CustomTrackWidth, true );
        }
        else if( id == ID_POPUP_PCB_SELECT_AUTO_WIDTH )
        {
            bds.UseCustomTrackViaSize( false );
            bds.m_UseConnectedTrackWidth = true;
        }
        else if( id == ID_POPUP_PCB_SELECT_USE_NETCLASS_VALUES )
        {
            bds.UseCustomTrackViaSize( false );
            bds.m_UseConnectedTrackWidth = false;
            bds.SetViaSizeIndex( 0 );
            bds.SetTrackWidthIndex( 0 );
        }
        else if( id >= ID_POPUP_PCB_SELECT_VIASIZE1 && id <= ID_POPUP_PCB_SELECT_VIASIZE16 )
        {
            bds.UseCustomTrackViaSize( false );
            bds.m_UseConnectedTrackWidth = false;
            bds.SetViaSizeIndex( id - ID_POPUP_PCB_SELECT_VIASIZE1 );
        }
        else if( id >= ID_POPUP_PCB_SELECT_WIDTH1 && id <= ID_POPUP_PCB_SELECT_WIDTH16 )
        {
            bds.UseCustomTrackViaSize( false );
            bds.m_UseConnectedTrackWidth = false;
            bds.SetTrackWidthIndex( id - ID_POPUP_PCB_SELECT_WIDTH1 );
        }

        return OPT_TOOL_EVENT( PCB_ACTIONS::trackViaSizeChanged.MakeEvent() );
    }

private:
    PCB_EDIT_FRAME& m_frame;
};


class DIFF_PAIR_MENU : public ACTION_MENU
{
public:
    DIFF_PAIR_MENU( PCB_EDIT_FRAME& aFrame ) :
        ACTION_MENU( true ),
        m_frame( aFrame )
    {
        SetIcon( width_track_via_xpm );
        SetTitle( _( "Select Differential Pair Dimensions" ) );
    }

protected:
    ACTION_MENU* create() const override
    {
        return new DIFF_PAIR_MENU( m_frame );
    }

    void update() override
    {
        EDA_UNITS                    units = m_frame.GetUserUnits();
        const BOARD_DESIGN_SETTINGS& bds = m_frame.GetBoard()->GetDesignSettings();

        Clear();

        Append( ID_POPUP_PCB_SELECT_USE_NETCLASS_DIFFPAIR, _( "Use Net Class Values" ),
                _( "Use differential pair dimensions from the net class" ), wxITEM_CHECK );
        Check( ID_POPUP_PCB_SELECT_USE_NETCLASS_DIFFPAIR,
               !bds.UseCustomDiffPairDimensions() && bds.GetDiffPairIndex() == 0 );

        Append( ID_POPUP_PCB_SELECT_CUSTOM_DIFFPAIR, _( "Use Custom Values..." ),
                _( "Specify custom differential pair dimensions" ), wxITEM_CHECK );
        Check( ID_POPUP_PCB_SELECT_CUSTOM_DIFFPAIR, bds.UseCustomDiffPairDimensions() );

        AppendSeparator();

        // Append the list of differential pair dimensions

        // Drop index 0 which is the current netclass dimensions (which are handled above)
        for( unsigned i = 1; i < bds.m_DiffPairDimensionsList.size(); ++i )
        {
            DIFF_PAIR_DIMENSION diffPair = bds.m_DiffPairDimensionsList[i];
            wxString            msg;

            if( diffPair.m_Gap <= 0 )
            {
                if( diffPair.m_ViaGap <= 0 )
                {
                    msg.Printf( _( "Width %s" ),
                                    MessageTextFromValue( units, diffPair.m_Width ) );
                }
                else
                {
                    msg.Printf( _( "Width %s, via gap %s" ),
                                    MessageTextFromValue( units, diffPair.m_Width ),
                                    MessageTextFromValue( units, diffPair.m_ViaGap ) );
                }
            }
            else
            {
                if( diffPair.m_ViaGap <= 0 )
                {
                    msg.Printf( _( "Width %s, gap %s" ),
                                    MessageTextFromValue( units, diffPair.m_Width ),
                                    MessageTextFromValue( units, diffPair.m_Gap ) );
                }
                else
                {
                    msg.Printf( _( "Width %s, gap %s, via gap %s" ),
                                    MessageTextFromValue( units, diffPair.m_Width ),
                                    MessageTextFromValue( units, diffPair.m_Gap ),
                                    MessageTextFromValue( units, diffPair.m_ViaGap ) );
                }
            }

            int menuIdx = ID_POPUP_PCB_SELECT_DIFFPAIR1 + i - 1;
            Append( menuIdx, msg, wxEmptyString, wxITEM_CHECK );
            Check( menuIdx, !bds.UseCustomDiffPairDimensions() && bds.GetDiffPairIndex() == i );
        }
    }

    OPT_TOOL_EVENT eventHandler( const wxMenuEvent& aEvent ) override
    {
        BOARD_DESIGN_SETTINGS &bds = m_frame.GetBoard()->GetDesignSettings();
        int id = aEvent.GetId();

        // On Windows, this handler can be called with an event ID not existing in any
        // menuitem, so only set flags when we have an ID match.

        if( id == ID_POPUP_PCB_SELECT_CUSTOM_DIFFPAIR )
        {
            bds.UseCustomDiffPairDimensions( true );
            TOOL_MANAGER* toolManager = m_frame.GetToolManager();
            toolManager->RunAction( PCB_ACTIONS::routerDiffPairDialog, true );
        }
        else if( id == ID_POPUP_PCB_SELECT_USE_NETCLASS_DIFFPAIR )
        {
            bds.UseCustomDiffPairDimensions( false );
            bds.SetDiffPairIndex( 0 );
        }
        else if( id >= ID_POPUP_PCB_SELECT_DIFFPAIR1 && id <= ID_POPUP_PCB_SELECT_DIFFPAIR16 )
        {
            bds.UseCustomDiffPairDimensions( false );
            // remember that the menu doesn't contain index 0 (which is the netclass values)
            bds.SetDiffPairIndex( id - ID_POPUP_PCB_SELECT_DIFFPAIR1 + 1 );
        }

        return OPT_TOOL_EVENT( PCB_ACTIONS::trackViaSizeChanged.MakeEvent() );
    }

private:
    PCB_EDIT_FRAME& m_frame;
};


ROUTER_TOOL::~ROUTER_TOOL()
{
}


bool ROUTER_TOOL::Init()
{
    PCB_EDIT_FRAME* frame = getEditFrame<PCB_EDIT_FRAME>();

    wxASSERT( frame );

    auto& menu = m_menu.GetMenu();
    menu.SetTitle( _( "Interactive Router" ) );

    auto trackViaMenu = std::make_shared<TRACK_WIDTH_MENU>( *frame );
    trackViaMenu->SetTool( this );
    m_menu.AddSubMenu( trackViaMenu );

    auto diffPairMenu = std::make_shared<DIFF_PAIR_MENU>( *frame );
    diffPairMenu->SetTool( this );
    m_menu.AddSubMenu( diffPairMenu );

    menu.AddItem( ACTIONS::cancelInteractive,        SELECTION_CONDITIONS::ShowAlways );

    menu.AddSeparator();

    menu.AddItem( PCB_ACTIONS::routeSingleTrack,     SELECTION_CONDITIONS::ShowAlways );
    menu.AddItem( PCB_ACTIONS::routeDiffPair,        SELECTION_CONDITIONS::ShowAlways );
    menu.AddItem( ACT_EndTrack,                      SELECTION_CONDITIONS::ShowAlways );
    menu.AddItem( ACT_UndoLastSegment,               SELECTION_CONDITIONS::ShowAlways );
    menu.AddItem( PCB_ACTIONS::breakTrack,           SELECTION_CONDITIONS::ShowAlways );

    menu.AddItem( PCB_ACTIONS::drag45Degree,         SELECTION_CONDITIONS::ShowAlways );
    menu.AddItem( PCB_ACTIONS::dragFreeAngle,        SELECTION_CONDITIONS::ShowAlways );

//        Add( ACT_AutoEndRoute );  // fixme: not implemented yet. Sorry.
    menu.AddItem( ACT_PlaceThroughVia,               SELECTION_CONDITIONS::ShowAlways );
    menu.AddItem( ACT_PlaceBlindVia,                 SELECTION_CONDITIONS::ShowAlways );
    menu.AddItem( ACT_PlaceMicroVia,                 SELECTION_CONDITIONS::ShowAlways );
    menu.AddItem( ACT_SelLayerAndPlaceThroughVia,    SELECTION_CONDITIONS::ShowAlways );
    menu.AddItem( ACT_SelLayerAndPlaceBlindVia,      SELECTION_CONDITIONS::ShowAlways );
    menu.AddItem( ACT_SwitchPosture,                 SELECTION_CONDITIONS::ShowAlways );
    menu.AddItem( ACT_SwitchRounding,                SELECTION_CONDITIONS::ShowAlways );

    menu.AddSeparator();

    auto diffPairCond =
        [this]( const SELECTION& )
        {
            return m_router->Mode() == PNS::PNS_MODE_ROUTE_DIFF_PAIR;
        };

    menu.AddMenu( trackViaMenu.get(),                SELECTION_CONDITIONS::ShowAlways );
    menu.AddMenu( diffPairMenu.get(),                diffPairCond );

    menu.AddItem( PCB_ACTIONS::routerSettingsDialog, SELECTION_CONDITIONS::ShowAlways );

    menu.AddSeparator();

    frame->AddStandardSubMenus( m_menu );

    return true;
}


void ROUTER_TOOL::Reset( RESET_REASON aReason )
{
    if( aReason == RUN )
        TOOL_BASE::Reset( aReason );
}


void ROUTER_TOOL::handleCommonEvents( const TOOL_EVENT& aEvent )
{
#ifdef DEBUG
    if( aEvent.IsKeyPressed() )
    {
        switch( aEvent.KeyCode() )
        {
        case '0':
        {
            auto logger = m_router->Logger();
            if( ! logger )
                return;

            FILE *f = fopen("/tmp/pns.log", "wb");
            wxLogTrace( "PNS", "saving drag/route log...\n" );

            const auto& events = logger->GetEvents();

            for( auto evt : events)
            {
                wxString id = "null";
                if( evt.item && evt.item->Parent() )
                    id = evt.item->Parent()->m_Uuid.AsString();

                fprintf(f, "event %d %d %d %s\n", evt.p.x, evt.p.y, evt.type, (const char*) id.c_str() );
            }

            fclose(f);

            // Export as *.kicad_pcb format, using a strategy which is specifically chosen
            // as an example on how it could also be used to send it to the system clipboard.

            PCB_IO  pcb_io;

            pcb_io.Save("/tmp/pns.dump", m_iface->GetBoard(), nullptr );

            break;
        }
        }
    }
#endif
}


int ROUTER_TOOL::getStartLayer( const PNS::ITEM* aItem )
{
    int tl = getView()->GetTopLayer();

    if( m_startItem )
    {
        const LAYER_RANGE& ls = m_startItem->Layers();

        if( ls.Overlaps( tl ) )
            return tl;
        else
            return ls.Start();
    }

    return tl;
}


void ROUTER_TOOL::switchLayerOnViaPlacement()
{
    int al = frame()->GetActiveLayer();
    int cl = m_router->GetCurrentLayer();

    if( cl != al )
    {
        m_router->SwitchLayer( al );
    }

    OPT<int> newLayer = m_router->Sizes().PairedLayer( cl );

    if( !newLayer )
        newLayer = m_router->Sizes().GetLayerTop();

    m_router->SwitchLayer( *newLayer );
    frame()->SetActiveLayer( ToLAYER_ID( *newLayer ) );
}


static VIATYPE getViaTypeFromFlags( int aFlags )
{
    switch( aFlags & VIA_ACTION_FLAGS::VIA_MASK )
    {
    case VIA_ACTION_FLAGS::VIA:
        return VIATYPE::THROUGH;
    case VIA_ACTION_FLAGS::BLIND_VIA:
        return VIATYPE::BLIND_BURIED;
    case VIA_ACTION_FLAGS::MICROVIA:
        return VIATYPE::MICROVIA;
    default:
        wxASSERT_MSG( false, "Unhandled via type" );
        return VIATYPE::THROUGH;
    }
}


static PCB_LAYER_ID getTargetLayerFromEvent( const TOOL_EVENT& aEvent )
{
    if( aEvent.IsAction( &PCB_ACTIONS::layerTop ) )
        return F_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner1 ) )
        return In1_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner2 ) )
        return In2_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner3 ) )
        return In3_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner4 ) )
        return In4_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner5 ) )
        return In5_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner6 ) )
        return In6_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner7 ) )
        return In7_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner8 ) )
        return In8_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner9 ) )
        return In9_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner10 ) )
        return In10_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner11 ) )
        return In11_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner12 ) )
        return In12_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner13 ) )
        return In13_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner14 ) )
        return In14_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner15 ) )
        return In15_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner16 ) )
        return In16_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner17 ) )
        return In17_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner18 ) )
        return In18_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner19 ) )
        return In19_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner20 ) )
        return In20_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner21 ) )
        return In21_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner22 ) )
        return In22_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner23 ) )
        return In23_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner24 ) )
        return In24_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner25 ) )
        return In25_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner26 ) )
        return In26_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner27 ) )
        return In27_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner28 ) )
        return In28_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner29 ) )
        return In29_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerInner30 ) )
        return In30_Cu;
    else if( aEvent.IsAction( &PCB_ACTIONS::layerBottom ) )
        return B_Cu;
    else
        return UNDEFINED_LAYER;
}


int ROUTER_TOOL::onLayerCommand( const TOOL_EVENT& aEvent )
{
    return handleLayerSwitch( aEvent, false );
}


int ROUTER_TOOL::onViaCommand( const TOOL_EVENT& aEvent )
{
    return handleLayerSwitch( aEvent, true );
}


int ROUTER_TOOL::handleLayerSwitch( const TOOL_EVENT& aEvent, bool aForceVia )
{
    if( !IsToolActive() )
        return 0;

    // First see if this is one of the switch layer commands
    LSEQ         layers       = LSET( board()->GetEnabledLayers() & LSET::AllCuMask() ).Seq();
    PCB_LAYER_ID currentLayer = (PCB_LAYER_ID) m_router->GetCurrentLayer();
    PCB_LAYER_ID targetLayer  = UNDEFINED_LAYER;

    if( aEvent.IsAction( &PCB_ACTIONS::layerNext ) )
    {
        size_t idx = 0;

        for( size_t i = 0; i < layers.size(); i++ )
        {
            if( layers[i] == currentLayer )
            {
                idx = i;
                break;
            }
        }

        idx         = ( idx + 1 ) % layers.size();
        targetLayer = layers[idx];
    }
    else if( aEvent.IsAction( &PCB_ACTIONS::layerPrev ) )
    {
        size_t idx = 0;

        for( size_t i = 0; i < layers.size(); i++ )
        {
            if( layers[i] == currentLayer )
            {
                idx = i;
                break;
            }
        }

        idx         = ( idx > 0 ) ? ( idx - 1 ) : ( layers.size() - 1 );
        targetLayer = layers[idx];
    }
    else
    {
        targetLayer = getTargetLayerFromEvent( aEvent );

        if( targetLayer != UNDEFINED_LAYER )
        {
            if( targetLayer == currentLayer )
                return 0;
        }
    }

    if( !aForceVia && m_router && m_router->SwitchLayer( targetLayer ) )
    {
        updateEndItem( aEvent );
        m_router->Move( m_endSnapPoint, m_endItem );        // refresh
        return 0;
    }

    BOARD_DESIGN_SETTINGS& bds        = board()->GetDesignSettings();
    const int              layerCount = bds.GetCopperLayerCount();

    PCB_LAYER_ID pairTop    = frame()->GetScreen()->m_Route_Layer_TOP;
    PCB_LAYER_ID pairBottom = frame()->GetScreen()->m_Route_Layer_BOTTOM;

    PNS::SIZES_SETTINGS sizes = m_router->Sizes();

    VIATYPE viaType     = VIATYPE::THROUGH;
    bool    selectLayer = false;

    // Otherwise it is one of the router-specific via commands
    if( targetLayer == UNDEFINED_LAYER )
    {
        const int actViaFlags = aEvent.Parameter<intptr_t>();
        selectLayer           = actViaFlags & VIA_ACTION_FLAGS::SELECT_LAYER;

        viaType = getViaTypeFromFlags( actViaFlags );

        // ask the user for a target layer
        if( selectLayer )
        {
            wxPoint dlgPosition = wxGetMousePosition();

            targetLayer = frame()->SelectLayer( static_cast<PCB_LAYER_ID>( currentLayer ),
                                                LSET::AllNonCuMask(), dlgPosition );

            // Reset the cursor to the position where the event occured
            controls()->SetCursorPosition( aEvent.HasPosition() ? aEvent.Position() : dlgPosition );
        }
    }

    // fixme: P&S supports more than one fixed layer pair. Update the dialog?
    sizes.ClearLayerPairs();

    if( !m_router->IsPlacingVia() )
    {
        // Cannot place microvias or blind vias if not allowed (obvious)
        if( ( viaType == VIATYPE::BLIND_BURIED ) && ( !bds.m_BlindBuriedViaAllowed ) )
        {
            WX_INFOBAR* infobar = frame()->GetInfoBar();
            wxHyperlinkCtrl* button = new wxHyperlinkCtrl( infobar, wxID_ANY,
                                                           _( "Show board setup" ),
                                                           wxEmptyString );

            button->Bind( wxEVT_COMMAND_HYPERLINK, std::function<void( wxHyperlinkEvent& aEvent )>(
                    [&]( wxHyperlinkEvent& aEvent )
                    {
                        getEditFrame<PCB_EDIT_FRAME>()->ShowBoardSetupDialog( _( "Constraints" ) );
                    } ) );

            infobar->RemoveAllButtons();
            infobar->AddButton( button );

            infobar->ShowMessageFor( _( "Blind/buried vias have to be enabled in "
                                        "Board Setup > Design Rules > Constraints." ),
                                     10000, wxICON_ERROR );
            return false;
        }

        if( ( viaType == VIATYPE::MICROVIA ) && ( !bds.m_MicroViasAllowed ) )
        {
            WX_INFOBAR* infobar = frame()->GetInfoBar();
            wxHyperlinkCtrl* button = new wxHyperlinkCtrl( infobar, wxID_ANY,
                                                           _( "Show board setup" ), wxEmptyString );

            button->Bind( wxEVT_COMMAND_HYPERLINK, std::function<void( wxHyperlinkEvent& aEvent )>(
                    [&]( wxHyperlinkEvent& aEvent )
                    {
                        getEditFrame<PCB_EDIT_FRAME>()->ShowBoardSetupDialog( _( "Constraints" ) );
                    } ) );

            infobar->RemoveAllButtons();
            infobar->AddButton( button );

            infobar->ShowMessageFor( _( "Microvias have to be enabled in "
                                        "Board Setup > Design Rules > Constraints." ),
                                     10000, wxICON_ERROR );
            return false;
        }

        // Can only place through vias on 2-layer boards
        if( ( viaType != VIATYPE::THROUGH ) && ( layerCount <= 2 ) )
        {
            frame()->ShowInfoBarError( _( "Only through vias are allowed on 2 layer boards." ) );
            return false;
        }

        // Can only place microvias if we're on an outer layer, or directly adjacent to one
        if( ( viaType == VIATYPE::MICROVIA ) && ( currentLayer > In1_Cu )
                && ( currentLayer < layerCount - 2 ) )
        {
            frame()->ShowInfoBarError( _( "Microvias can only be placed between the outer layers "
                                          "(F.Cu/B.Cu) and the ones directly adjacent to them." ) );
            return false;
        }
    }

    // Convert blind/buried via to a through hole one, if it goes through all layers
    if( viaType == VIATYPE::BLIND_BURIED
            && ( ( targetLayer == B_Cu && currentLayer == F_Cu )
                       || ( targetLayer == F_Cu && currentLayer == B_Cu ) ) )
    {
        viaType = VIATYPE::THROUGH;
    }

    switch( viaType )
    {
    case VIATYPE::THROUGH:
        if( targetLayer == UNDEFINED_LAYER )
        {
            // use the default layer pair
            currentLayer = pairTop;
            targetLayer = pairBottom;
        }
        break;

    case VIATYPE::MICROVIA:
        wxASSERT_MSG( !selectLayer, "Unexpected select layer for microvia (microvia layers are "
                                    "implicit)" );

        if( currentLayer == F_Cu || currentLayer == In1_Cu )
        {
            // front-side microvia
            currentLayer = F_Cu;
            targetLayer = In1_Cu;
        }
        else if( currentLayer == B_Cu || currentLayer == layerCount - 2 )
        {
            // back-side microvia
            currentLayer = B_Cu,
            targetLayer = (PCB_LAYER_ID) ( layerCount - 2 );
        }
        else
        {
            wxASSERT_MSG( false, "Invalid layer pair for microvia (must be on or adjacent to an "
                                 "outer layer)" );
        }
        break;

    case VIATYPE::BLIND_BURIED:
        if( targetLayer == UNDEFINED_LAYER )
        {
            if( currentLayer == pairTop || currentLayer == pairBottom )
            {
                // the current layer is on the defined layer pair,
                // swap to the other side
                currentLayer = pairTop;
                targetLayer = pairBottom;
            }
            else
            {
                // the current layer is not part of the current layer pair,
                // so fallback and swap to the top layer of the pair by default
                targetLayer = pairTop;
            }
        }
        break;

    default:
        wxASSERT( false );
        break;
    }

    sizes.SetViaDiameter( bds.m_ViasMinSize );
    sizes.SetViaDrill( bds.m_MinThroughDrill );

    if( bds.UseNetClassVia() || viaType == VIATYPE::MICROVIA )
    {
        class VIA dummyVia( board() );
        dummyVia.SetViaType( viaType );
        dummyVia.SetLayerPair( currentLayer, targetLayer );

        if( !m_router->GetCurrentNets().empty() )
            dummyVia.SetNetCode( m_router->GetCurrentNets()[0] );

        DRC_CONSTRAINT constraint;

        constraint = bds.m_DRCEngine->EvalRulesForItems( VIA_DIAMETER_CONSTRAINT, &dummyVia,
                                                         nullptr, currentLayer );

        if( !constraint.IsNull() )
            sizes.SetViaDiameter( constraint.m_Value.Opt() );

        constraint = bds.m_DRCEngine->EvalRulesForItems( HOLE_SIZE_CONSTRAINT, &dummyVia, nullptr,
                                                         currentLayer );

        if( !constraint.IsNull() )
            sizes.SetViaDrill( constraint.m_Value.Opt() );
    }
    else
    {
        sizes.SetViaDiameter( bds.GetCurrentViaSize() );
        sizes.SetViaDrill( bds.GetCurrentViaDrill() );
    }

    sizes.SetViaType( viaType );
    sizes.AddLayerPair( currentLayer, targetLayer );

    m_router->UpdateSizes( sizes );
    m_router->ToggleViaPlacement();

    if( m_router->RoutingInProgress() )
        updateEndItem( aEvent );
    else
        updateStartItem( aEvent );

    m_router->Move( m_endSnapPoint, m_endItem );        // refresh

    return 0;
}


bool ROUTER_TOOL::prepareInteractive()
{
    int routingLayer = getStartLayer( m_startItem );

    if( !IsCopperLayer( routingLayer ) )
    {
        frame()->ShowInfoBarError( _( "Tracks on Copper layers only" ) );
        return false;
    }

    PCB_EDIT_FRAME* editFrame = getEditFrame<PCB_EDIT_FRAME>();

    editFrame->SetActiveLayer( ToLAYER_ID( routingLayer ) );

    if( m_startItem && m_startItem->Net() >= 0 )
        highlightNet( true, m_startItem->Net() );

    controls()->ForceCursorPosition( false );
    controls()->SetAutoPan( true );

    PNS::SIZES_SETTINGS sizes( m_router->Sizes() );

    m_iface->ImportSizes( sizes, m_startItem, -1 );
    sizes.AddLayerPair( frame()->GetScreen()->m_Route_Layer_TOP,
                        frame()->GetScreen()->m_Route_Layer_BOTTOM );

    m_router->UpdateSizes( sizes );

    if( !m_router->StartRouting( m_startSnapPoint, m_startItem, routingLayer ) )
    {
        frame()->ShowInfoBarError( m_router->FailureReason() );
        highlightNet( false );
        controls()->SetAutoPan( false );
        return false;
    }

    m_endItem = nullptr;
    m_endSnapPoint = m_startSnapPoint;

    frame()->UndoRedoBlock( true );

    return true;
}


bool ROUTER_TOOL::finishInteractive()
{
    m_router->StopRouting();

    frame()->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );
    controls()->SetAutoPan( false );
    controls()->ForceCursorPosition( false );
    frame()->UndoRedoBlock( false );
    highlightNet( false );

    return true;
}


void ROUTER_TOOL::performRouting()
{
    if( !prepareInteractive() )
        return;

    auto setCursor =
            [&]()
            {
                frame()->GetCanvas()->SetCurrentCursor( KICURSOR::PENCIL );
            };

    // Set initial cursor
    setCursor();

    while( TOOL_EVENT* evt = Wait() )
    {
        setCursor();

        // Don't crash if we missed an operation that cancelled routing.
        if( !m_router->RoutingInProgress() )
        {
            if( evt->IsCancelInteractive() )
                m_cancelled = true;

            break;
        }

        handleCommonEvents( *evt );

        if( evt->IsMotion() )
        {
            m_router->SetOrthoMode( evt->Modifier( MD_CTRL ) );
            updateEndItem( *evt );
            m_router->Move( m_endSnapPoint, m_endItem );
        }
        else if( evt->IsAction( &ACT_UndoLastSegment ) )
        {
            m_router->UndoLastSegment();
            updateEndItem( *evt );
            m_router->Move( m_endSnapPoint, m_endItem );
        }
        else if( evt->IsClick( BUT_LEFT ) || evt->IsAction( &PCB_ACTIONS::routeSingleTrack ) )
        {
            updateEndItem( *evt );
            bool needLayerSwitch = m_router->IsPlacingVia();
            bool forceFinish = evt->Modifier( MD_SHIFT );

            if( m_router->FixRoute( m_endSnapPoint, m_endItem, forceFinish ) )
            {
                break;
            }

            if( needLayerSwitch )
                switchLayerOnViaPlacement();

            // Synchronize the indicated layer
            frame()->SetActiveLayer( ToLAYER_ID( m_router->GetCurrentLayer() ) );
            updateEndItem( *evt );
            m_router->Move( m_endSnapPoint, m_endItem );
            m_startItem = nullptr;
        }
        else if( evt->IsAction( &ACT_SwitchRounding ) )
        {
            m_router->ToggleRounded();
            updateEndItem( *evt );
            m_router->Move( m_endSnapPoint, m_endItem );        // refresh
        }
        else if( evt->IsAction( &ACT_SwitchPosture ) )
        {
            m_router->FlipPosture();
            updateEndItem( *evt );
            m_router->Move( m_endSnapPoint, m_endItem );        // refresh
        }
        else if( evt->IsAction( &ACT_EndTrack ) || evt->IsDblClick( BUT_LEFT )  )
        {
            // Stop current routing:
            m_router->FixRoute( m_endSnapPoint, m_endItem, true );
            break;
        }
        else if( evt->IsCancelInteractive() || evt->IsActivate()
                 || evt->IsUndoRedo()
                 || evt->IsAction( &PCB_ACTIONS::routerInlineDrag ) )
        {
            if( evt->IsCancelInteractive() && !m_router->RoutingInProgress() )
                m_cancelled = true;

            if( evt->IsActivate() && !evt->IsMoveTool() )
                m_cancelled = true;

            break;
        }
        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu( selection() );
        }
        else
        {
            evt->SetPassEvent();
        }
    }

    m_router->CommitRouting();
    m_router->StopRouting();

    finishInteractive();
}


int ROUTER_TOOL::DpDimensionsDialog( const TOOL_EVENT& aEvent )
{
    PNS::SIZES_SETTINGS sizes = m_router->Sizes();
    DIALOG_PNS_DIFF_PAIR_DIMENSIONS settingsDlg( frame(), sizes );

    if( settingsDlg.ShowModal() == wxID_OK )
    {
        m_router->UpdateSizes( sizes );
        m_savedSizes = sizes;

        BOARD_DESIGN_SETTINGS& bds = frame()->GetBoard()->GetDesignSettings();
        bds.SetCustomDiffPairWidth( sizes.DiffPairWidth() );
        bds.SetCustomDiffPairGap( sizes.DiffPairGap() );
        bds.SetCustomDiffPairViaGap( sizes.DiffPairViaGap() );
    }

    return 0;
}


int ROUTER_TOOL::SettingsDialog( const TOOL_EVENT& aEvent )
{
    DIALOG_PNS_SETTINGS settingsDlg( frame(), m_router->Settings() );

    settingsDlg.ShowModal();

    return 0;
}


int ROUTER_TOOL::ChangeRouterMode( const TOOL_EVENT& aEvent )
{
    PNS::PNS_MODE mode = aEvent.Parameter<PNS::PNS_MODE>();
    PNS::ROUTING_SETTINGS& settings = m_router->Settings();

    settings.SetMode( mode );

    return 0;
}


PNS::PNS_MODE ROUTER_TOOL::GetRouterMode()
{
    return m_router->Settings().Mode();
}


void ROUTER_TOOL::breakTrack()
{
    if( m_startItem && m_startItem->OfKind( PNS::ITEM::SEGMENT_T ) )
        m_router->BreakSegment( m_startItem, m_startSnapPoint );
}


int ROUTER_TOOL::MainLoop( const TOOL_EVENT& aEvent )
{
    PNS::ROUTER_MODE mode = aEvent.Parameter<PNS::ROUTER_MODE>();
    PCB_EDIT_FRAME*  frame = getEditFrame<PCB_EDIT_FRAME>();

    if( m_router->RoutingInProgress() )
    {
        if( m_router->Mode() == mode )
            return 0;
        else
            m_router->StopRouting();
    }

    // Deselect all items
    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );

    std::string tool = aEvent.GetCommandStr().get();
    frame->PushTool( tool );
    Activate();

    m_router->SetMode( mode );

    VIEW_CONTROLS* ctls = getViewControls();
    ctls->ShowCursor( true );
    ctls->ForceCursorPosition( false );
    m_cancelled = false;

    // Prime the pump
    if( aEvent.HasPosition() )
        m_toolMgr->PrimeTool( ctls->GetCursorPosition( false ) );

    auto setCursor =
            [&]()
            {
                frame->GetCanvas()->SetCurrentCursor( KICURSOR::PENCIL );
            };

    // Set initial cursor
    setCursor();

    // Main loop: keep receiving events
    while( TOOL_EVENT* evt = Wait() )
    {
        setCursor();

        if( evt->IsCancelInteractive() )
        {
            frame->PopTool( tool );
            break;
        }
        else if( evt->IsActivate() )
        {
            if( evt->IsMoveTool() )
            {
                // leave ourselves on the stack so we come back after the move
                break;
            }
            else
            {
                frame->PopTool( tool );
                break;
            }
        }
        else if( evt->Action() == TA_UNDO_REDO_PRE )
        {
            m_router->ClearWorld();
        }
        else if( evt->Action() == TA_UNDO_REDO_POST || evt->Action() == TA_MODEL_CHANGE )
        {
            m_router->SyncWorld();
        }
        else if( evt->IsMotion() )
        {
            updateStartItem( *evt );
        }
        else if( evt->IsAction( &PCB_ACTIONS::dragFreeAngle ) )
        {
            updateStartItem( *evt, true );
            performDragging( PNS::DM_ANY | PNS::DM_FREE_ANGLE );
        }
        else if( evt->IsAction( &PCB_ACTIONS::drag45Degree ) )
        {
            updateStartItem( *evt, true );
            performDragging( PNS::DM_ANY );
        }
        else if( evt->IsAction( &PCB_ACTIONS::breakTrack ) )
        {
            updateStartItem( *evt, true );
            breakTrack( );
        }
        else if( evt->IsClick( BUT_LEFT )
              || evt->IsAction( &PCB_ACTIONS::routeSingleTrack )
              || evt->IsAction( &PCB_ACTIONS::routeDiffPair ) )
        {
            updateStartItem( *evt );

            if( evt->HasPosition() )
            {
                if( evt->Modifier( MD_CTRL ) )
                    performDragging( PNS::DM_ANY );
                else
                    performRouting();
            }
        }
        else if( evt->IsAction( &ACT_PlaceThroughVia ) )
        {
            m_toolMgr->RunAction( PCB_ACTIONS::layerToggle, true );
        }
        else if( evt->IsAction( &PCB_ACTIONS::layerChanged ) )
        {
            m_router->SwitchLayer( frame->GetActiveLayer() );
            updateStartItem( *evt );
        }
        else if( evt->IsKeyPressed() )
        {
            // wxWidgets fails to correctly translate shifted keycodes on the wxEVT_CHAR_HOOK
            // event so we need to process the wxEVT_CHAR event that will follow as long as we
            // pass the event.
            evt->SetPassEvent();
        }
        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu( selection() );
        }
        else
        {
            evt->SetPassEvent();
        }

        if( m_cancelled )
        {
            frame->PopTool( tool );
            break;
        }
    }

    // Store routing settings till the next invocation
    m_savedSizes = m_router->Sizes();

    return 0;
}


void ROUTER_TOOL::performDragging( int aMode )
{
    VIEW_CONTROLS* ctls = getViewControls();

    if( m_startItem && m_startItem->IsLocked() )
    {
        KIDIALOG dlg( frame(), _( "The selected item is locked." ), _( "Confirmation" ),
                      wxOK | wxCANCEL | wxICON_WARNING );
        dlg.SetOKLabel( _( "Drag Anyway" ) );
        dlg.DoNotShowCheckbox( __FILE__, __LINE__ );

        if( dlg.ShowModal() == wxID_CANCEL )
            return;
    }

    bool dragStarted = m_router->StartDragging( m_startSnapPoint, m_startItem, aMode );

    if( !dragStarted )
        return;

    if( m_startItem && m_startItem->Net() >= 0 )
        highlightNet( true, m_startItem->Net() );

    ctls->SetAutoPan( true );
    m_gridHelper->SetAuxAxes( true, m_startSnapPoint );
    frame()->UndoRedoBlock( true );

    while( TOOL_EVENT* evt = Wait() )
    {
        ctls->ForceCursorPosition( false );

        if( evt->IsMotion() )
        {
            updateEndItem( *evt );
            m_router->Move( m_endSnapPoint, m_endItem );
        }
        else if( evt->IsClick( BUT_LEFT ) )
        {
            if( m_router->FixRoute( m_endSnapPoint, m_endItem ) )
                break;
        }
        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu( selection() );
        }
        else if( evt->IsCancelInteractive() || evt->IsActivate() || evt->IsUndoRedo() )
        {
            if( evt->IsCancelInteractive() && !m_startItem )
                m_cancelled = true;

            if( evt->IsActivate() && !evt->IsMoveTool() )
                m_cancelled = true;

            break;
        }
        else
        {
            evt->SetPassEvent();
        }

        handleCommonEvents( *evt );
    }

    if( m_router->RoutingInProgress() )
        m_router->StopRouting();

    m_startItem = nullptr;

    m_gridHelper->SetAuxAxes( false );
    frame()->UndoRedoBlock( false );
    ctls->SetAutoPan( false );
    ctls->ForceCursorPosition( false );
    highlightNet( false );
}


void ROUTER_TOOL::NeighboringSegmentFilter( const VECTOR2I& aPt, GENERAL_COLLECTOR& aCollector )
{
    /*
     * If the collection contains a trivial line corner (two connected segments)
     * or a non-fanout-via (a via with no more than two connected segments), then
     * trim the collection down to a single item (which one won't matter since
     * they're all connected).
     */

    // First make sure we've got something that *might* match.
    int vias = aCollector.CountType( PCB_VIA_T );
    int traces = aCollector.CountType( PCB_TRACE_T );
    int arcs = aCollector.CountType( PCB_ARC_T );

    if( arcs > 0 || vias > 1 || traces > 2 || vias + traces < 1 )
        return;

    // Fetch first TRACK (via or trace) as our reference
    TRACK* reference = nullptr;

    for( int i = 0; !reference && i < aCollector.GetCount(); i++ )
        reference = dynamic_cast<TRACK*>( aCollector[i] );

    int refNet = reference->GetNetCode();

    wxPoint refPoint( aPt.x, aPt.y );
    STATUS_FLAGS flags = reference->IsPointOnEnds( refPoint, -1 );

    if( flags & STARTPOINT )
        refPoint = reference->GetStart();
    else if( flags & ENDPOINT )
        refPoint = reference->GetEnd();

    // Check all items to ensure that any TRACKs are co-terminus with the reference and on
    // the same net.
    for( int i = 0; i < aCollector.GetCount(); i++ )
    {
        TRACK* neighbor = dynamic_cast<TRACK*>( aCollector[i] );

        if( neighbor && neighbor != reference )
        {
            if( neighbor->GetNetCode() != refNet )
                return;

            if( neighbor->GetStart() != refPoint && neighbor->GetEnd() != refPoint )
                return;
        }
    }

    // Selection meets criteria; trim it to the reference item.
    aCollector.Empty();
    aCollector.Append( reference );
}


bool ROUTER_TOOL::CanInlineDrag()
{
    m_toolMgr->RunAction( PCB_ACTIONS::selectionCursor, true, NeighboringSegmentFilter );
    const auto& selection = m_toolMgr->GetTool<SELECTION_TOOL>()->GetSelection();

    if( selection.Size() == 1 )
    {
        const BOARD_ITEM* item = static_cast<const BOARD_ITEM*>( selection.Front() );

        if( item->Type() == PCB_TRACE_T
             || item->Type() == PCB_VIA_T
             || item->Type() == PCB_FOOTPRINT_T )
        {
            return true;
        }
    }

    return false;
}


int ROUTER_TOOL::InlineDrag( const TOOL_EVENT& aEvent )
{
    const auto& selection = m_toolMgr->GetTool<SELECTION_TOOL>()->GetSelection();

    if( selection.Empty() )
        m_toolMgr->RunAction( PCB_ACTIONS::selectionCursor, true, NeighboringSegmentFilter );

    if( selection.Size() != 1 )
        return 0;

    const BOARD_ITEM* item = static_cast<const BOARD_ITEM*>( selection.Front() );

    if( item->Type() != PCB_TRACE_T
         && item->Type() != PCB_VIA_T
         && item->Type() != PCB_FOOTPRINT_T )
    {
        return 0;
    }

    Activate();

    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );
    m_router->SyncWorld();
    m_startItem = nullptr;

    PNS::ITEM* startItem = nullptr;
    PNS::ITEM_SET itemsToDrag;
    const FOOTPRINT* footprint = nullptr;

    if( item->Type() == PCB_FOOTPRINT_T )
    {
        footprint = static_cast<const FOOTPRINT*>(item);

        for( const PAD* pad : footprint->Pads() )
        {
            PNS::ITEM* solid = m_router->GetWorld()->FindItemByParent( pad );

            if( solid )
                itemsToDrag.Add( solid );
        }
    }
    else
    {
        startItem = m_router->GetWorld()->FindItemByParent( item );

        if( startItem)
            itemsToDrag.Add( startItem );
    }

    if( startItem && startItem->IsLocked() )
    {
        KIDIALOG dlg( frame(), _( "The selected item is locked." ), _( "Confirmation" ),
                      wxOK | wxCANCEL | wxICON_WARNING );
        dlg.SetOKLabel( _( "Drag Anyway" ) );
        dlg.DoNotShowCheckbox( __FILE__, __LINE__ );

        if( dlg.ShowModal() == wxID_CANCEL )
            return 0;
    }

    VECTOR2I p0 = controls()->GetCursorPosition( false );
    VECTOR2I p = p0;

    if( startItem )
        p = snapToItem( true, startItem, p0 );

    int dragMode = aEvent.Parameter<int64_t> ();

    bool dragStarted = m_router->StartDragging( p, itemsToDrag, dragMode );

    if( !dragStarted )
        return 0;

    m_gridHelper->SetAuxAxes( true, p );
    controls()->ShowCursor( true );
    controls()->ForceCursorPosition( false );
    controls()->SetAutoPan( true );
    frame()->UndoRedoBlock( true );

    view()->ClearPreview();
    view()->InitPreview();

    auto setCursor =
            [&]()
            {
                frame()->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );
            };

    // Set initial cursor
    setCursor();

    while( TOOL_EVENT* evt = Wait() )
    {
        setCursor();

        if( evt->IsCancelInteractive() )
        {
            break;
        }
        else if( evt->IsMotion() || evt->IsDrag( BUT_LEFT ) )
        {
            updateEndItem( *evt );
            m_router->Move( m_endSnapPoint, m_endItem );

            if( footprint )
            {
                VECTOR2I offset = m_endSnapPoint - p;
                BOARD_ITEM* previewItem;

                view()->ClearPreview();

                for( BOARD_ITEM* drawing : footprint->GraphicalItems() )
                {
                    previewItem = static_cast<BOARD_ITEM*>( drawing->Clone() );

                    if( drawing->Type() == PCB_FP_SHAPE_T )
                    {
                        FP_SHAPE* shape = static_cast<FP_SHAPE*>( previewItem );
                        shape->PCB_SHAPE::Move( (wxPoint) offset );
                    }
                    else
                    {
                        previewItem->Move( offset );
                    }

                    view()->AddToPreview( previewItem );
                    view()->Hide( drawing, true );
                }

                previewItem = static_cast<BOARD_ITEM*>( footprint->Reference().Clone() );
                previewItem->Move( offset );
                view()->AddToPreview( previewItem );
                view()->Hide( &footprint->Reference() );

                previewItem = static_cast<BOARD_ITEM*>( footprint->Value().Clone() );
                previewItem->Move( offset );
                view()->AddToPreview( previewItem );
                view()->Hide( &footprint->Value() );

                for( ZONE* zone : footprint->Zones() )
                {
                    previewItem = static_cast<BOARD_ITEM*>( zone->Clone() );
                    previewItem->Move( offset );
                    view()->AddToPreview( previewItem );
                    view()->Hide( zone, true );
                }
            }
        }
        else if( evt->IsMouseUp( BUT_LEFT ) || evt->IsClick( BUT_LEFT ) )
        {
            updateEndItem( *evt );
            m_router->FixRoute( m_endSnapPoint, m_endItem );
            break;
        }
        else if( evt->Category() == TC_COMMAND )
        {
            // disallow editing commands
            if( evt->IsAction( &ACTIONS::cut )
                || evt->IsAction( &ACTIONS::copy )
                || evt->IsAction( &ACTIONS::paste )
                || evt->IsAction( &ACTIONS::pasteSpecial ) )
            {
                wxBell();
            }
            else
            {
                evt->SetPassEvent();
            }
        }
        else
        {
            evt->SetPassEvent();
        }
    }

    if( footprint )
    {
        for( BOARD_ITEM* drawing : footprint->GraphicalItems() )
            view()->Hide( drawing, false );

        view()->Hide( &footprint->Reference(), false );
        view()->Hide( &footprint->Value(), false );

        for( ZONE* zone : footprint->Zones() )
            view()->Hide( zone, false );

        view()->ClearPreview();
        view()->ShowPreview( false );
    }

    if( m_router->RoutingInProgress() )
        m_router->StopRouting();

    m_gridHelper->SetAuxAxes( false );
    controls()->SetAutoPan( false );
    controls()->ForceCursorPosition( false );
    frame()->UndoRedoBlock( false );

    return 0;
}


int ROUTER_TOOL::InlineBreakTrack( const TOOL_EVENT& aEvent )
{
    const auto& selection = m_toolMgr->GetTool<SELECTION_TOOL>()->GetSelection();

    if( selection.Size() != 1 )
        return 0;

    const BOARD_CONNECTED_ITEM* item = static_cast<const BOARD_CONNECTED_ITEM*>( selection.Front() );

    if( item->Type() != PCB_TRACE_T )
        return 0;

    Activate();

    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );
    m_router->SyncWorld();
    m_startItem = m_router->GetWorld()->FindItemByParent( item );

    TOOL_MANAGER* toolManager = frame()->GetToolManager();

    if( toolManager->IsContextMenuActive() )
    {
        // If we're here from a context menu then we need to get the position of the
        // cursor when the context menu was invoked.  This is used to figure out the
        // break point on the track.
        VECTOR2I CurrPos = toolManager->GetMenuCursorPos();
        m_startSnapPoint = snapToItem( true, m_startItem, toolManager->GetMenuCursorPos() );
    }
    else
    {
        // If we're here from a hotkey, then get the current mouse position so we know
        // where to break the track.
        m_startSnapPoint = snapToItem( true, m_startItem, controls()->GetCursorPosition() );
    }

    if( m_startItem && m_startItem->IsLocked() )
    {
        KIDIALOG dlg( frame(), _( "The selected item is locked." ), _( "Confirmation" ),
                      wxOK | wxCANCEL | wxICON_WARNING );
        dlg.SetOKLabel( _( "Break Track" ) );
        dlg.DoNotShowCheckbox( __FILE__, __LINE__ );

        if( dlg.ShowModal() == wxID_CANCEL )
            return 0;
    }

    frame()->UndoRedoBlock( true );
    breakTrack();

    if( m_router->RoutingInProgress() )
        m_router->StopRouting();

    frame()->UndoRedoBlock( false );

    return 0;
}


int ROUTER_TOOL::CustomTrackWidthDialog( const TOOL_EVENT& aEvent )
{
    BOARD_DESIGN_SETTINGS& bds = board()->GetDesignSettings();
    DIALOG_TRACK_VIA_SIZE sizeDlg( frame(), bds );

    if( sizeDlg.ShowModal() )
    {
        bds.UseCustomTrackViaSize( true );

        TOOL_EVENT dummy;
        onTrackViaSizeChanged( dummy );
    }

    return 0;
}


int ROUTER_TOOL::onTrackViaSizeChanged( const TOOL_EVENT& aEvent )
{
    PNS::SIZES_SETTINGS sizes( m_router->Sizes() );

    if( !m_router->GetCurrentNets().empty() )
        m_iface->ImportSizes( sizes, nullptr, m_router->GetCurrentNets()[0] );

    m_router->UpdateSizes( sizes );

    // Changing the track width can affect the placement, so call the
    // move routine without changing the destination
    m_router->Move( m_endSnapPoint, m_endItem );

    return 0;
}


void ROUTER_TOOL::setTransitions()
{
    Go( &ROUTER_TOOL::SelectCopperLayerPair,  PCB_ACTIONS::selectLayerPair.MakeEvent() );

    Go( &ROUTER_TOOL::MainLoop,               PCB_ACTIONS::routeSingleTrack.MakeEvent() );
    Go( &ROUTER_TOOL::MainLoop,               PCB_ACTIONS::routeDiffPair.MakeEvent() );
    Go( &ROUTER_TOOL::DpDimensionsDialog,     PCB_ACTIONS::routerDiffPairDialog.MakeEvent() );
    Go( &ROUTER_TOOL::SettingsDialog,         PCB_ACTIONS::routerSettingsDialog.MakeEvent() );
    Go( &ROUTER_TOOL::ChangeRouterMode,       PCB_ACTIONS::routerHighlightMode.MakeEvent() );
    Go( &ROUTER_TOOL::ChangeRouterMode,       PCB_ACTIONS::routerShoveMode.MakeEvent() );
    Go( &ROUTER_TOOL::ChangeRouterMode,       PCB_ACTIONS::routerWalkaroundMode.MakeEvent() );
    Go( &ROUTER_TOOL::InlineDrag,             PCB_ACTIONS::routerInlineDrag.MakeEvent() );
    Go( &ROUTER_TOOL::InlineBreakTrack,       PCB_ACTIONS::inlineBreakTrack.MakeEvent() );

    Go( &ROUTER_TOOL::onViaCommand,           ACT_PlaceThroughVia.MakeEvent() );
    Go( &ROUTER_TOOL::onViaCommand,           ACT_PlaceBlindVia.MakeEvent() );
    Go( &ROUTER_TOOL::onViaCommand,           ACT_PlaceMicroVia.MakeEvent() );
    Go( &ROUTER_TOOL::onViaCommand,           ACT_SelLayerAndPlaceThroughVia.MakeEvent() );
    Go( &ROUTER_TOOL::onViaCommand,           ACT_SelLayerAndPlaceBlindVia.MakeEvent() );

    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerTop.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner1.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner2.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner3.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner4.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner5.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner6.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner7.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner8.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner9.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner10.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner11.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner12.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner13.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner14.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner15.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner16.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner17.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner18.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner19.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner20.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner21.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner22.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner23.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner24.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner25.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner26.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner27.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner28.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner29.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerInner30.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerBottom.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerNext.MakeEvent() );
    Go( &ROUTER_TOOL::onLayerCommand,         PCB_ACTIONS::layerPrev.MakeEvent() );

    Go( &ROUTER_TOOL::CustomTrackWidthDialog, ACT_CustomTrackWidth.MakeEvent() );
    Go( &ROUTER_TOOL::onTrackViaSizeChanged,  PCB_ACTIONS::trackViaSizeChanged.MakeEvent() );
}
