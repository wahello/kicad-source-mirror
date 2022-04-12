/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2019-2022 KiCad Developers, see AUTHORS.TXT for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef QA_EESCHEMA_EESCHEMA_TEST_UTILS__H
#define QA_EESCHEMA_EESCHEMA_TEST_UTILS__H


#include <schematic.h>
#include <settings/settings_manager.h>
#include <sch_io_mgr.h>
#include <wx/filename.h>

#include <connection_graph.h>
#include <netlist_exporter_kicad.h>
#include <netlist_exporter_spice.h>
#include <netlist_reader/netlist_reader.h>
#include <netlist_reader/pcb_netlist.h>
#include <project.h>
#include <sch_io_mgr.h>
#include <sch_sheet.h>
#include <schematic.h>
#include <settings/settings_manager.h>
#include <wildcards_and_files_ext.h>

namespace KI_TEST
{
/**
 * Get the configured location of Eeschema test data.
 *
 * By default, this is the test data in the source tree, but can be overridden
 * by the KICAD_TEST_EESCHEMA_DATA_DIR environment variable.
 *
 * @return a filename referring to the test data dir to use.
 */
wxFileName GetEeschemaTestDataDir();

/**
 * A generic fixture for loading schematics and associated settings for qa tests
 */
class SCHEMATIC_TEST_FIXTURE
{
public:
    SCHEMATIC_TEST_FIXTURE() : m_schematic( nullptr ), m_manager( true )
    {
        m_pi = SCH_IO_MGR::FindPlugin( SCH_IO_MGR::SCH_KICAD );
    }

    virtual ~SCHEMATIC_TEST_FIXTURE()
    {
        m_schematic.Reset();
        delete m_pi;
    }

protected:
    virtual void loadSchematic( const wxString& aRelativePath );

    virtual wxFileName getSchematicFile( const wxString& aBaseName );

    ///> Schematic to load
    SCHEMATIC m_schematic;

    SCH_PLUGIN* m_pi;

    SETTINGS_MANAGER m_manager;
};


} // namespace KI_TEST


template <typename Exporter>
class TEST_NETLIST_EXPORTER_FIXTURE
{
public:
    TEST_NETLIST_EXPORTER_FIXTURE() :
            m_schematic( nullptr ),
            m_pi( SCH_IO_MGR::FindPlugin( SCH_IO_MGR::SCH_KICAD ) ),
            m_manager( true )
    {
        
    }

    virtual ~TEST_NETLIST_EXPORTER_FIXTURE()
    {
        m_schematic.Reset();
        SCH_IO_MGR::ReleasePlugin( m_pi );
    }

    void LoadSchematic( const wxString& aBaseName );

    virtual wxString GetSchematicPath( const wxString& aBaseName );
    virtual wxString GetNetlistPath( bool aTest = false );
    virtual unsigned GetNetlistOptions() { return 0; }

    void WriteNetlist();

    virtual void CompareNetlists() = 0;

    void Cleanup();

    void TestNetlist( const wxString& aBaseName );

    ///> Schematic to load
    SCHEMATIC m_schematic;

    SCH_PLUGIN* m_pi;

    SETTINGS_MANAGER m_manager;
};

template class TEST_NETLIST_EXPORTER_FIXTURE<NETLIST_EXPORTER_KICAD>;
template class TEST_NETLIST_EXPORTER_FIXTURE<NETLIST_EXPORTER_SPICE>;

#endif // QA_EESCHEMA_EESCHEMA_TEST_UTILS__H
