/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2022 Fabien Corona f.corona<at>laposte.net
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include "kibis.h"
#include "ibis_parser.h"
#include <sstream>

KIBIS_ANY::KIBIS_ANY( KIBIS* aTopLevel ) : IBIS_ANY( aTopLevel->m_reporter )
{
    m_topLevel = aTopLevel;
    m_valid = false;
}


IBIS_CORNER ReverseLogic( IBIS_CORNER aIn )
{
    IBIS_CORNER out = IBIS_CORNER::TYP;

    if( aIn == IBIS_CORNER::MIN )
    {
        out = IBIS_CORNER::MAX;
    }
    else if( aIn == IBIS_CORNER::MAX )
    {
        out = IBIS_CORNER::MIN;
    }

    return out;
}

KIBIS::KIBIS( std::string aFileName ) : KIBIS_ANY( this ), m_file( this )
{
    IBIS_REPORTER reporter;
    IbisParser    parser( &reporter );

    parser.m_parrot = false;
    parser.ParseFile( aFileName );

    bool status = true;

    status &= m_file.Init( parser );

    for( IbisModel& iModel : parser.m_ibisFile.m_models )
    {
        KIBIS_MODEL kModel( this, iModel, parser );
        status &= kModel.m_valid;
        m_models.push_back( kModel );
    }

    for( IbisComponent& iComponent : parser.m_ibisFile.m_components )
    {
        KIBIS_COMPONENT kComponent( this, iComponent, parser );

        status &= kComponent.m_valid;
        m_components.push_back( kComponent );

        for( KIBIS_PIN& pin : m_components.back().m_pins )
        {
            pin.m_parent = &( m_components.back() );
        }
    }

    m_valid = status;
}


KIBIS_FILE::KIBIS_FILE( KIBIS* aTopLevel ) : KIBIS_ANY( aTopLevel )
{
}

bool KIBIS_FILE::Init( IbisParser& aParser )
{
    bool status = true;
    m_fileName = aParser.m_ibisFile.m_header->m_fileName;
    m_fileRev = aParser.m_ibisFile.m_header->m_fileRevision;
    m_ibisVersion = aParser.m_ibisFile.m_header->m_ibisVersion;
    m_date = aParser.m_ibisFile.m_header->m_date;
    m_notes = aParser.m_ibisFile.m_header->m_notes;
    m_disclaimer = aParser.m_ibisFile.m_header->m_disclaimer;
    m_copyright = aParser.m_ibisFile.m_header->m_copyright;

    m_valid = status;

    return status;
}

KIBIS_PIN::KIBIS_PIN( KIBIS* aTopLevel, IbisComponentPin& aPin, IbisComponentPackage& aPackage,
                      IbisParser& aParser, KIBIS_COMPONENT* aParent,
                      std::vector<KIBIS_MODEL>& aModels ) :
        KIBIS_ANY( aTopLevel )
{
    m_signalName = aPin.m_signalName;
    m_pinNumber = aPin.m_pinName;
    m_parent = aParent;

    R_pin = aPackage.m_Rpkg;
    L_pin = aPackage.m_Lpkg;
    C_pin = aPackage.m_Cpkg;

    // The values listed in the [Pin] description section override the default
    // values defined in [Package]

    // @TODO : Reading the IBIS standard, I can't figure out if we are supposed
    // to replace typ, min, and max, or just the typ ?

    if( !isnan( aPin.m_Lpin ) )
    {
        R_pin->value[IBIS_CORNER::TYP] = aPin.m_Rpin;
        R_pin->value[IBIS_CORNER::MIN] = aPin.m_Rpin;
        R_pin->value[IBIS_CORNER::MAX] = aPin.m_Rpin;
    }
    if( !isnan( aPin.m_Lpin ) )
    {
        L_pin->value[IBIS_CORNER::TYP] = aPin.m_Lpin;
        L_pin->value[IBIS_CORNER::MIN] = aPin.m_Lpin;
        L_pin->value[IBIS_CORNER::MAX] = aPin.m_Lpin;
    }
    if( !isnan( aPin.m_Cpin ) )
    {
        C_pin->value[IBIS_CORNER::TYP] = aPin.m_Cpin;
        C_pin->value[IBIS_CORNER::MIN] = aPin.m_Cpin;
        C_pin->value[IBIS_CORNER::MAX] = aPin.m_Cpin;
    }

    bool                     modelSelected = false;
    std::vector<std::string> listOfModels;

    for( IbisModelSelector modelSelector : aParser.m_ibisFile.m_modelSelectors )
    {
        if( !strcmp( modelSelector.m_name.c_str(), aPin.m_modelName.c_str() ) )
        {
            for( IbisModelSelectorEntry model : modelSelector.m_models )
            {
                listOfModels.push_back( model.m_modelName );
            }
            modelSelected = true;
            break;
        }
    }

    if( !modelSelected )
    {
        listOfModels.push_back( aPin.m_modelName );
    }

    for( std::string modelName : listOfModels )
    {
        for( KIBIS_MODEL& model : aModels )
        {
            if( !strcmp( model.m_name.c_str(), modelName.c_str() ) )
            {
                m_models.push_back( &model );
            }
        }
    }
}

KIBIS_MODEL::KIBIS_MODEL( KIBIS* aTopLevel, IbisModel& aSource, IbisParser& aParser ) :
        KIBIS_ANY( aTopLevel )
{
    bool status = true;

    m_name = aSource.m_name;
    m_type = aSource.m_type;

    m_description = std::string( "No description available." );

    for( IbisModelSelector modelSelector : aParser.m_ibisFile.m_modelSelectors )
    {
        for( IbisModelSelectorEntry entry : modelSelector.m_models )
        {
            if( !strcmp( entry.m_modelName.c_str(), m_name.c_str() ) )
            {
                m_description = entry.m_modelDescription;
            }
        }
    }

    m_vinh = aSource.m_vinh;
    m_vinl = aSource.m_vinl;
    m_vref = aSource.m_vref;
    m_rref = aSource.m_rref;
    m_cref = aSource.m_cref;
    m_vmeas = aSource.m_vmeas;

    m_enable = aSource.m_enable;
    m_polarity = aSource.m_polarity;

    m_ramp = aSource.m_ramp;
    m_risingWaveforms = aSource.m_risingWaveforms;
    m_fallingWaveforms = aSource.m_fallingWaveforms;
    m_GNDClamp = aSource.m_GNDClamp;
    m_GNDClampReference = aSource.m_GNDClampReference;
    m_POWERClamp = aSource.m_POWERClamp;
    m_POWERClampReference = aSource.m_POWERClampReference;

    m_C_comp = aSource.m_C_comp;
    m_voltageRange = aSource.m_voltageRange;
    m_temperatureRange = aSource.m_temperatureRange;
    m_pullupReference = aSource.m_pullupReference;
    m_pulldownReference = aSource.m_pulldownReference;

    m_Rgnd = aSource.m_Rgnd;
    m_Rpower = aSource.m_Rpower;
    m_Rac = aSource.m_Rac;
    m_Cac = aSource.m_Cac;
    m_pullup = aSource.m_pullup;
    m_pulldown = aSource.m_pulldown;

    m_valid = status;
}

KIBIS_COMPONENT::KIBIS_COMPONENT( KIBIS* aTopLevel, IbisComponent& aSource, IbisParser& aParser ) :
        KIBIS_ANY( aTopLevel )
{
    bool status = true;

    m_name = aSource.m_name;
    m_manufacturer = aSource.m_manufacturer;
    m_topLevel = aTopLevel;

    for( IbisComponentPin& iPin : aSource.m_pins )
    {
        if( iPin.m_dummy )
        {
            continue;
        }

        KIBIS_PIN kPin( aTopLevel, iPin, aSource.m_package, aParser, nullptr,
                        m_topLevel->m_models );
        status &= kPin.m_valid;
        m_pins.push_back( kPin );
    }

    m_valid = status;
}

KIBIS_PIN* KIBIS_COMPONENT::getPin( std::string aPinNumber )
{
    for( KIBIS_PIN& pin : m_pins )
    {
        if( pin.m_pinNumber == aPinNumber )
        {
            return &pin;
        }
    }

    return nullptr;
}

std::vector<std::pair<IbisWaveform*, IbisWaveform*>> KIBIS_MODEL::waveformPairs()
{
    std::vector<std::pair<IbisWaveform*, IbisWaveform*>> pairs;
    IbisWaveform*                                        wf1;
    IbisWaveform*                                        wf2;

    for( int i = 0; i < m_risingWaveforms.size(); i++ )
    {
        for( int j = 0; j < m_fallingWaveforms.size(); j++ )
        {
            wf1 = m_risingWaveforms.at( i );
            wf2 = m_fallingWaveforms.at( j );

            if( wf1->m_R_fixture == wf2->m_R_fixture && wf1->m_L_fixture == wf2->m_L_fixture
                && wf1->m_C_fixture == wf2->m_C_fixture && wf1->m_V_fixture == wf2->m_V_fixture
                && wf1->m_V_fixture_min == wf2->m_V_fixture_min
                && wf1->m_V_fixture_max == wf2->m_V_fixture_max )
            {
                std::pair<IbisWaveform*, IbisWaveform*> p;
                p.first = wf1;
                p.second = wf2;
                pairs.push_back( p );
            }
        }
    }

    return pairs;
}

std::string KIBIS_MODEL::SpiceDie( IBIS_CORNER aSupply, IBIS_CORNER aParasitics, int aIndex )
{
    std::string result;

    std::string GC_GND = "GC_GND";
    std::string PC_PWR = "PC_PWR";
    std::string PU_PWR = "PU_PWR";
    std::string PD_GND = "PD_GND";
    std::string DIE = "DIE";
    std::string DIEBUFF = "DIEBUFF";

    GC_GND += std::to_string( aIndex );
    PC_PWR += std::to_string( aIndex );
    PU_PWR += std::to_string( aIndex );
    PD_GND += std::to_string( aIndex );
    DIE += std::to_string( aIndex );
    DIEBUFF += std::to_string( aIndex );


    std::string GC = "GC";
    std::string PC = "PC";
    std::string PU = "PU";
    std::string PD = "PD";

    GC += std::to_string( aIndex );
    PC += std::to_string( aIndex );
    PU += std::to_string( aIndex );
    PD += std::to_string( aIndex );

    result = "\n";
    result += "VPWR POWER GND ";
    result += doubleToString( m_voltageRange->value[aSupply] );
    result += "\n";
    result += "CCPOMP " + DIE + " GND ";
    result += doubleToString( m_C_comp->value[aParasitics] );
    result += "\n";

    if( HasGNDClamp() )
    {
        result += m_GNDClamp->Spice( aIndex * 4 + 1, DIE, GC_GND, GC, aSupply );
        result += "VmeasGC GND " + GC_GND + " 0\n";
    }

    if( HasPOWERClamp() )
    {
        result += m_POWERClamp->Spice( aIndex * 4 + 2, "POWER", DIE, PC, aSupply );
        result += "VmeasPC POWER " + PC_PWR + " 0\n";
    }

    if( HasPulldown() )
    {
        result += m_pulldown->Spice( aIndex * 4 + 3, DIEBUFF, PD_GND, PD, aSupply );
        result += "VmeasPD GND " + PD_GND + " 0\n";
        result += "BKD GND " + DIE + " i=( -i(VmeasPU) * v(KU) )\n";
    }

    if( HasPullup() )
    {
        result += m_pullup->Spice( aIndex * 4 + 4, PU_PWR, DIEBUFF, PU, aSupply );
        result += "VmeasPU POWER " + PU_PWR + " 0\n";
        result += "BKU POWER " + DIE + " i=( i(VmeasPD) * v(KD) )\n";
    }

    result += "BDIEBUFF " + DIEBUFF + " GND v=v(" + DIE + ")\n";

    return result;
}

IbisWaveform KIBIS_MODEL::TrimWaveform( IbisWaveform& aIn )
{
    IbisWaveform out( aIn.m_reporter );

    int nbPoints = aIn.m_table->m_entries.size();

    if( nbPoints < 2 )
    {
        Report( _( "waveform has less than two points" ), RPT_SEVERITY_ERROR );
        return out;
    }

    double DCtyp = aIn.m_table->m_entries[0].V->value[IBIS_CORNER::TYP];
    double DCmin = aIn.m_table->m_entries[0].V->value[IBIS_CORNER::MIN];
    double DCmax = aIn.m_table->m_entries[0].V->value[IBIS_CORNER::MAX];

    if( nbPoints == 2 )
    {
        return out;
    }

    out.m_table->m_entries.clear();
    bool kept = false;

    for( int i = 0; i < nbPoints; i++ )
    {
        VTtableEntry entry( out.m_reporter );

        entry.t = aIn.m_table->m_entries.at( i ).t;
        entry.V->value[IBIS_CORNER::TYP] =
                aIn.m_table->m_entries.at( i ).V->value[IBIS_CORNER::TYP];
        entry.V->value[IBIS_CORNER::MIN] =
                aIn.m_table->m_entries.at( i ).V->value[IBIS_CORNER::MIN];
        entry.V->value[IBIS_CORNER::MAX] =
                aIn.m_table->m_entries.at( i ).V->value[IBIS_CORNER::MAX];
        out.m_table->m_entries.push_back( entry );
        out.m_table->m_entries.at( i ).V->value[IBIS_CORNER::TYP] -= DCtyp;
        out.m_table->m_entries.at( i ).V->value[IBIS_CORNER::MIN] -= DCmin;
        out.m_table->m_entries.at( i ).V->value[IBIS_CORNER::MAX] -= DCmax;
    }

    return out;
}

bool KIBIS_MODEL::HasPulldown()
{
    return m_pulldown->m_entries.size() > 0;
}
bool KIBIS_MODEL::HasPullup()
{
    return m_pullup->m_entries.size() > 0;
}
bool KIBIS_MODEL::HasGNDClamp()
{
    return m_GNDClamp->m_entries.size() > 0;
}
bool KIBIS_MODEL::HasPOWERClamp()
{
    return m_POWERClamp->m_entries.size() > 0;
}

std::string KIBIS_MODEL::generateSquareWave( std::string aNode1, std::string aNode2,
                                             std::vector<std::pair<int, double>>     aBits,
                                             std::pair<IbisWaveform*, IbisWaveform*> aPair,
                                             IBIS_CORNER                             aSupply )
{
    std::string simul;

    IbisWaveform risingWF = TrimWaveform( *( aPair.first ) );
    IbisWaveform fallingWF = TrimWaveform( *( aPair.second ) );

    double deltaR = risingWF.m_table->m_entries.back().V->value[aSupply]
                    - risingWF.m_table->m_entries.at( 0 ).V->value[aSupply];
    double deltaF = fallingWF.m_table->m_entries.back().V->value[aSupply]
                    - fallingWF.m_table->m_entries.at( 0 ).V->value[aSupply];

    // Ideally, delta should be equal to zero.
    // It can be different from zero if the falling waveform does not start were the rising one ended.
    double delta = deltaR + deltaF;

    int i = 0;
    for( std::pair<int, double> bit : aBits )
    {
        IbisWaveform* WF;
        double        timing = bit.second;

        if( bit.first == 1 )
            WF = &risingWF;
        else
            WF = &fallingWF;

        simul += "Vstimuli";
        simul += std::to_string( i );
        simul += " stimuli";
        simul += std::to_string( i );
        simul += " ";
        simul += aNode2;
        simul += " pwl ( ";

        if( i != 0 )
        {
            simul += "0 0 ";
            VTtableEntry entry0 = WF->m_table->m_entries.at( 0 );
            VTtableEntry entry1 = WF->m_table->m_entries.at( 1 );
            double       deltaT = entry1.t - entry0.t;

            simul += doubleToString( entry0.t + timing - deltaT );
            simul += " ";
            simul += "0";
            simul += " ";
        }

        for( VTtableEntry& entry : WF->m_table->m_entries )
        {
            simul += doubleToString( entry.t + timing );
            simul += " ";
            simul += doubleToString( entry.V->value[aSupply] - delta );
            simul += " ";
        }
        simul += ")\n";

        i++;
    }

    simul += "bin ";
    simul += aNode1;
    simul += " ";
    simul += aNode2;
    simul += " v=(";

    for( int i = 0; i < aBits.size(); i++ )
    {
        simul += " v( stimuli";
        simul += std::to_string( i );
        simul += " ) + ";
    }

    // Depending on the first bit, we add a different DC value
    // The DC value we add is the first value of the first bit.
    if( aBits[0].first == 0 )
    {
        simul += doubleToString( aPair.second->m_table->m_entries.at( 0 ).V->value[aSupply] );
    }
    else
    {
        simul += doubleToString( aPair.first->m_table->m_entries.at( 0 ).V->value[aSupply] );
    }

    simul += ")\n";
    return simul;
}

std::string KIBIS_PIN::addDie( KIBIS_MODEL& aModel, IBIS_CORNER aSupply, int aIndex )
{
    std::string simul;

    std::string GC_GND = "GC_GND";
    std::string PC_PWR = "PC_PWR";
    std::string PU_PWR = "PU_PWR";
    std::string PD_GND = "PD_GND";
    std::string DIE = "DIE";

    GC_GND += std::to_string( aIndex );
    PC_PWR += std::to_string( aIndex );
    PU_PWR += std::to_string( aIndex );
    PD_GND += std::to_string( aIndex );
    DIE += std::to_string( aIndex );


    std::string GC = "GC";
    std::string PC = "PC";
    std::string PU = "PU";
    std::string PD = "PD";

    GC += std::to_string( aIndex );
    PC += std::to_string( aIndex );
    PU += std::to_string( aIndex );
    PD += std::to_string( aIndex );

    if( aModel.HasGNDClamp() )
    {
        simul += aModel.m_GNDClamp->Spice( aIndex * 4 + 1, DIE, GC_GND, GC, aSupply );
    }

    if( aModel.HasPOWERClamp() )
    {
        simul += aModel.m_POWERClamp->Spice( aIndex * 4 + 2, PC_PWR, DIE, PC, aSupply );
    }

    if( aModel.HasPulldown() )
    {
        simul += aModel.m_pulldown->Spice( aIndex * 4 + 3, DIE, PD_GND, PD, aSupply );
    }

    if( aModel.HasPullup() )
    {
        simul += aModel.m_pullup->Spice( aIndex * 4 + 4, PU_PWR, DIE, PU, aSupply );
    }

    return simul;
}

void KIBIS_PIN::getKuKdFromFile( std::string* aSimul )
{
    // @TODO
    // that's not the best way to do, but ¯\_(ツ)_/¯
    std::ifstream in( "temp_input.spice" );

    std::remove( "temp_input.spice" );
    std::remove( "temp_output.spice" );

    std::ofstream file( "temp_input.spice" );

    file << *aSimul;

    system( "ngspice temp_input.spice" );


    std::ifstream KuKdfile;
    KuKdfile.open( "temp_output.spice" );

    std::vector<double> ku, kd, t;
    if( KuKdfile )
    {
        std::string line;
        for( int i = 0; i < 11; i++ ) // number of line in the ngspice output header
        {
            std::getline( KuKdfile, line );
        }
        int    i = 0;
        double t_v, ku_v, kd_v;
        while( KuKdfile )
        {
            std::getline( KuKdfile, line );

            if( line.empty() )
            {
                continue;
            }
            switch( i )
            {
            case 0:
                line = line.substr( line.find_first_of( "\t" ) + 1 );
                t_v = std::stod( line );
                break;
            case 1: ku_v = std::stod( line ); break;
            case 2:
                kd_v = std::stod( line );
                ku.push_back( ku_v );
                kd.push_back( kd_v );
                t.push_back( t_v );
                break;
            default: Report( _( "Error while reading temporary file" ), RPT_SEVERITY_ERROR );
            }
            i = ( i + 1 ) % 3;
        }
        std::getline( KuKdfile, line );
    }
    else
    {
        Report( _( "Error while creating temporary file" ), RPT_SEVERITY_ERROR );
    }
    std::remove( "temp_input.spice" );
    std::remove( "temp_output.spice" );

    // @TODO : this is the end of the dirty code

    m_Ku = ku;
    m_Kd = kd;
    m_t = t;
}

std::string KIBIS_PIN::KuKdDriver( KIBIS_MODEL&                            aModel,
                                   std::pair<IbisWaveform*, IbisWaveform*> aPair,
                                   KIBIS_WAVEFORM* aWave, IBIS_CORNER aSupply, IBIS_CORNER aParasitics,
                                   int aIndex )
{
    std::string simul = "";

    simul += "*THIS IS NOT A VALID SPICE MODEL.\n";
    simul += "*This part is intended to be executed by Kibis internally.\n";
    simul += "*You should not be able to read this.\n\n";

    simul += ".SUBCKT DRIVER";
    simul += std::to_string( aIndex );
    simul += " POWER GND PIN \n"; // 1: POWER, 2:GND, 3:PIN

    if( ( aPair.first->m_R_dut == 0 ) && ( aPair.first->m_L_dut == 0 )
                && ( aPair.first->m_C_dut == 0 )
        || true )
    {
        simul += "Vdummy 2 PIN 0\n";
    }
    {
        /*
        simul += "RPIN 1 PIN ";
        simul << aPair.first->m_R_dut;
        simul += "\n";
        simul += "LPIN 2 1 ";
        simul << aPair.first->m_L_dut;
        simul += "\n";
        simul += "CPIN PIN GND ";
        simul << aPair.first->m_C_dut;
        simul += "\n";
        */
        Report( _( "Kibis does not support DUT values yet. "
                   "https://ibis.org/summits/nov16a/chen.pdf" ),
                RPT_SEVERITY_WARNING );
    }

    simul += "\n";
    simul += "CCPOMP 2 GND ";
    simul += doubleToString( aModel.m_C_comp->value[aParasitics] ); //@TODO: Check the corner ?
    simul += "\n";
    switch( aWave->GetType() )
    {
    case KIBIS_WAVEFORM_TYPE::RECTANGULAR:
    {
        std::vector<std::pair<int, double>> bits;
        KIBIS_WAVEFORM_RECTANGULAR* rectWave = static_cast<KIBIS_WAVEFORM_RECTANGULAR*>( aWave );

        IbisWaveform* risingWF = aPair.first;
        IbisWaveform* fallingWF = aPair.second;

        if( rectWave->m_ton < risingWF->m_table->m_entries.back().t )
        {
            Report( _( "Rising edge is longer than on time." ), RPT_SEVERITY_WARNING );
        }

        if( rectWave->m_toff < fallingWF->m_table->m_entries.back().t )
        {
            Report( _( "Falling edge is longer than off time." ), RPT_SEVERITY_WARNING );
        }

        for( int i = 0; i < rectWave->m_cycles; i++ )
        {
            std::pair<int, double> bit;
            bit.first = rectWave->inverted ? 0 : 1;
            bit.second = ( rectWave->m_ton + rectWave->m_toff ) * i + rectWave->m_delay;
            bits.push_back( bit );

            bit.first = rectWave->inverted ? 1 : 0;
            bit.second = ( rectWave->m_ton + rectWave->m_toff ) * i + rectWave->m_delay
                         + rectWave->m_ton;
            bits.push_back( bit );
        }

        simul += aModel.generateSquareWave( "DIE0", "GND", bits, aPair, aSupply );
        break;
    }
    case KIBIS_WAVEFORM_TYPE::STUCK_HIGH:
    {
        IbisWaveform* fallingWF = aPair.second;
        simul += "Vsig DIE0 GND ";
        simul += doubleToString( fallingWF->m_table->m_entries.at( 0 ).V->value[aSupply] );
        simul += "\n";
        break;
    }
    case KIBIS_WAVEFORM_TYPE::STUCK_LOW:
    {
        IbisWaveform* risingWF = aPair.first;
        simul += "Vsig DIE0 GND ";
        simul += doubleToString( risingWF->m_table->m_entries.at( 0 ).V->value[aSupply] );
        simul += "\n";
        break;
    }
    case KIBIS_WAVEFORM_TYPE::NONE:
    case KIBIS_WAVEFORM_TYPE::HIGH_Z:
    default: break;
    }
    simul += addDie( aModel, aSupply, 0 );

    simul += "\n.ENDS DRIVER\n\n";
    return simul;
}

void KIBIS_PIN::getKuKdOneWaveform( KIBIS_MODEL&                            aModel,
                                    std::pair<IbisWaveform*, IbisWaveform*> aPair,
                                    KIBIS_WAVEFORM* aWave, IBIS_CORNER aSupply, IBIS_CORNER aParasitics )
{
    std::string simul = "";

    if( aWave->GetType() == KIBIS_WAVEFORM_TYPE::NONE )
    {
        //@TODO , there could be some current flowing through pullup / pulldown transistors, even when off
        std::vector<double> ku, kd, t;
        ku.push_back( 0 );
        kd.push_back( 0 );
        t.push_back( 0 );
        m_Ku = ku;
        m_Kd = kd;
        m_t = t;
    }
    else
    {
        simul += KuKdDriver( aModel, aPair, aWave, aSupply, aParasitics, 0 );
        simul += "\n x1 3 0 1 DRIVER0 \n";

        simul += "VCC 3 0 ";
        simul += doubleToString( aModel.m_voltageRange->value[aSupply] );
        simul += "\n";
        //simul += "Vpin x1.DIE 0 1 \n"
        simul += "Lfixture 1 4 ";
        simul += doubleToString( aPair.first->m_L_fixture );
        simul += "\n";
        simul += "Rfixture 4 5 ";
        simul += doubleToString( aPair.first->m_R_fixture );
        simul += "\n";
        simul += "Cfixture 4 0 ";
        simul += doubleToString( aPair.first->m_C_fixture );
        simul += "\n";
        simul += "Vfixture 5 0 ";
        simul += doubleToString( aPair.first->m_V_fixture );
        simul += "\n";
        simul += "VmeasIout x1.DIE0 x1.2 0\n";
        simul += "VmeasPD 0 x1.PD_GND0 0\n";
        simul += "VmeasPU x1.PU_PWR0 3 0\n";
        simul += "VmeasPC x1.PC_PWR0 3 0\n";
        simul += "VmeasGC 0 x1.GC_PWR0 0\n";

        if( aModel.HasPullup() && aModel.HasPulldown() )
        {
            Report( _( "Model has only one waveform pair, reduced accuracy" ),
                    RPT_SEVERITY_WARNING );
            simul += "Bku KU 0 v=( (i(VmeasIout)-i(VmeasPC)-i(VmeasGC)-i(VmeasPD) "
                     ")/(i(VmeasPU)-i(VmeasPD)))\n";
            simul += "Bkd KD 0 v=(1-v(KU))\n";
        }

        else if( !aModel.HasPullup() && aModel.HasPulldown() )
        {
            simul += "Bku KD 0 v=( ( i(VmeasIout)+i(VmeasPC)+i(VmeasGC) )/(i(VmeasPD)))\n";
            simul += "Bkd KU 0 v=0\n";
        }

        else if( aModel.HasPullup() && !aModel.HasPulldown() )
        {
            simul += "Bku KU 0 v=( ( i(VmeasIout)+i(VmeasPC)+i(VmeasGC) )/(i(VmeasPU)))\n";
            simul += "Bkd KD 0 v=0\n";
        }
        else
        {
            Report( _( "Driver needs at least a pullup or a pulldown" ), RPT_SEVERITY_ERROR );
        }

        switch( aWave->GetType() )
        {
        case KIBIS_WAVEFORM_TYPE::RECTANGULAR: simul += ".tran 0.1n 1000n \n"; break;

        case KIBIS_WAVEFORM_TYPE::HIGH_Z:
        case KIBIS_WAVEFORM_TYPE::STUCK_LOW:
        case KIBIS_WAVEFORM_TYPE::STUCK_HIGH:
        default: simul += ".tran 0.5 1 \n"; //
        }
        //simul += ".dc Vpin -5 5 0.1\n";
        simul += ".control run \n";
        simul += "set filetype=ascii\n";
        simul += "run \n";
        simul += "plot v(x1.DIE0) i(VmeasIout) i(VmeasPD) i(VmeasPU) i(VmeasPC) i(VmeasGC)\n";
        simul += "plot v(KU) v(KD)\n";
        simul += "write temp_output.spice v(KU) v(KD)\n"; // @TODO we might want to remove this...
        //simul += "quit\n";
        simul += ".endc \n";
        simul += ".end \n";

        getKuKdFromFile( &simul );
    }
}

void KIBIS_PIN::getKuKdNoWaveform( KIBIS_MODEL& aModel, KIBIS_WAVEFORM* aWave, IBIS_CORNER aSupply )
{
    std::vector<double> ku, kd, t;

    switch( aWave->GetType() )
    {
    case KIBIS_WAVEFORM_TYPE::RECTANGULAR:
    {
        KIBIS_WAVEFORM_RECTANGULAR* rectWave = static_cast<KIBIS_WAVEFORM_RECTANGULAR*>( aWave );
        for( int i = 0; i < rectWave->m_cycles; i++ )
        {
            ku.push_back( 0 );
            kd.push_back( 1 );
            t.push_back( ( rectWave->m_ton + rectWave->m_toff ) * i );
            ku.push_back( 1 );
            kd.push_back( 0 );
            t.push_back( ( rectWave->m_ton + rectWave->m_toff ) * i
                         + aModel.m_ramp->m_rising->value[aSupply].m_dt
                                   / 0.6 ); // 0.6 because ibis only gives 20%-80% time
            ku.push_back( 1 );
            kd.push_back( 0 );
            t.push_back( ( rectWave->m_ton + rectWave->m_toff ) * i + rectWave->m_toff );
            ku.push_back( 0 );
            kd.push_back( 1 );
            t.push_back( ( rectWave->m_ton + rectWave->m_toff ) * i + rectWave->m_toff
                         + aModel.m_ramp->m_falling->value[aSupply].m_dt / 0.6 );
        }
        break;
    }
    case KIBIS_WAVEFORM_TYPE::STUCK_HIGH:
    {
        ku.push_back( 1 );
        kd.push_back( 0 );
        t.push_back( 0 );
        break;
    }
    case KIBIS_WAVEFORM_TYPE::STUCK_LOW:
    {
        ku.push_back( 0 );
        kd.push_back( 1 );
        t.push_back( 0 );
        break;
    }
    case KIBIS_WAVEFORM_TYPE::HIGH_Z:
    case KIBIS_WAVEFORM_TYPE::NONE:
    default:
        ku.push_back( 0 );
        kd.push_back( 0 );
        t.push_back( 0 );
    }
    m_Ku = ku;
    m_Kd = kd;
    m_t = t;
}

void KIBIS_PIN::getKuKdTwoWaveforms( KIBIS_MODEL&                            aModel,
                                     std::pair<IbisWaveform*, IbisWaveform*> aPair1,
                                     std::pair<IbisWaveform*, IbisWaveform*> aPair2,
                                     KIBIS_WAVEFORM* aWave, IBIS_CORNER aSupply,
                                     IBIS_CORNER aParasitics )
{
    std::string simul = "";

    if( aWave->GetType() == KIBIS_WAVEFORM_TYPE::NONE )
    {
        //@TODO , there could be some current flowing through pullup / pulldown transistors, even when off
        std::vector<double> ku, kd, t;
        ku.push_back( 0 );
        kd.push_back( 0 );
        t.push_back( 0 );
        m_Ku = ku;
        m_Kd = kd;
        m_t = t;
    }
    else
    {
        simul += KuKdDriver( aModel, aPair1, aWave, aSupply, aParasitics, 0 );
        simul += KuKdDriver( aModel, aPair2, aWave, aSupply, aParasitics, 1 );
        simul += "\n x1 3 0 1 DRIVER0 \n";

        simul += "VCC 3 0 ";
        simul += doubleToString( aModel.m_voltageRange->value[aSupply] );
        simul += "\n";
        //simul += "Vpin x1.DIE 0 1 \n"
        simul += "Lfixture0 1 4 ";
        simul += doubleToString( aPair1.first->m_L_fixture );
        simul += "\n";
        simul += "Rfixture0 4 5 ";
        simul += doubleToString( aPair1.first->m_R_fixture );
        simul += "\n";
        simul += "Cfixture0 4 0 ";
        simul += doubleToString( aPair1.first->m_C_fixture );
        simul += "\n";
        simul += "Vfixture0 5 0 ";
        simul += doubleToString( aPair1.first->m_V_fixture );
        simul += "\n";
        simul += "VmeasIout0 x1.2 x1.DIE0 0\n";
        simul += "VmeasPD0 0 x1.PD_GND0 0\n";
        simul += "VmeasPU0 x1.PU_PWR0 3 0\n";
        simul += "VmeasPC0 x1.PC_PWR0 3 0\n";
        simul += "VmeasGC0 0 x1.GC_PWR0 0\n";


        simul += "\n x2 3 0 7 DRIVER1 \n";
        //simul += "Vpin x1.DIE 0 1 \n"
        simul += "Lfixture1 7 8 ";
        simul += doubleToString( aPair2.first->m_L_fixture );
        simul += "\n";
        simul += "Rfixture1 8 9 ";
        simul += doubleToString( aPair2.first->m_R_fixture );
        simul += "\n";
        simul += "Cfixture1 8 0 ";
        simul += doubleToString( aPair2.first->m_C_fixture );
        simul += "\n";
        simul += "Vfixture1 9 0 ";
        simul += doubleToString( aPair2.first->m_V_fixture );
        simul += "\n";
        simul += "VmeasIout1 x2.2 x2.DIE0 0\n";
        simul += "VmeasPD1 0 x2.PD_GND0 0\n";
        simul += "VmeasPU1 x2.PU_PWR0 3 0\n";
        simul += "VmeasPC1 x2.PC_PWR0 3 0\n";
        simul += "VmeasGC1 0 x2.GC_PWR0 0\n";

        if( aModel.HasPullup() && aModel.HasPulldown() )
        {
            simul +=
                    "Bku KU 0 v=(  ( i(VmeasPD1) * ( i(VmeasIout0) + i(VmeasPC0) + i(VmeasGC0) ) - "
                    "i(VmeasPD0) * ( i(VmeasIout1) + i(VmeasPC1) + i(VmeasGC1) )  )/ ( i(VmeasPU1) "
                    "* "
                    "i(VmeasPD0) - i(VmeasPU0) * i(VmeasPD1)  ) )\n";
            simul +=
                    "Bkd KD 0 v=(  ( i(VmeasPU1) * ( i(VmeasIout0) + i(VmeasPC0) + i(VmeasGC0) ) - "
                    "i(VmeasPU0) * ( i(VmeasIout1) + i(VmeasPC1) + i(VmeasGC1) )  )/ ( i(VmeasPD1) "
                    "* "
                    "i(VmeasPU0) - i(VmeasPD0) * i(VmeasPU1)  ) )\n";
            //simul += "Bkd KD 0 v=(1-v(KU))\n";
        }

        else if( !aModel.HasPullup() && aModel.HasPulldown() )
        {
            Report( _( "There are two waveform pairs, but only one transistor. More equations than "
                       "unknowns." ),
                    RPT_SEVERITY_WARNING );
            simul += "Bku KD 0 v=( ( i(VmeasIout0)+i(VmeasPC0)+i(VmeasGC0) )/(i(VmeasPD0)))\n";
            simul += "Bkd KU 0 v=0\n";
        }

        else if( aModel.HasPullup() && !aModel.HasPulldown() )
        {
            Report( _( "There are two waveform pairs, but only one transistor. More equations than "
                       "unknowns." ),
                    RPT_SEVERITY_WARNING );
            simul += "Bku KU 0 v=( ( i(VmeasIout)+i(VmeasPC)+i(VmeasGC) )/(i(VmeasPU)))\n";
            simul += "Bkd KD 0 v=0\n";
        }
        else
        {
            Report( _( "Driver needs at least a pullup or a pulldown" ), RPT_SEVERITY_ERROR );
        }

        simul += ".tran 0.1n 1000n \n";
        //simul += ".dc Vpin -5 5 0.1\n";
        simul += ".control run \n";
        simul += "set filetype=ascii\n";
        simul += "run \n";
        simul += "plot v(KU) v(KD)\n";
        simul += "plot v(x1.DIE0) \n";
        simul += "write temp_output.spice v(KU) v(KD)\n"; // @TODO we might want to remove this...
        //simul += "quit\n";
        simul += ".endc \n";
        simul += ".end \n";

        getKuKdFromFile( &simul );
    }
}

bool KIBIS_PIN::writeSpiceDriver( std::string* aDest, std::string aName, KIBIS_MODEL& aModel,
                                  IBIS_CORNER aSupply, IBIS_CORNER aParasitics, KIBIS_ACCURACY aAccuracy,
                                  KIBIS_WAVEFORM* aWave )
{
    bool status = true;

    switch( aModel.m_type )
    {
    case IBIS_MODEL_TYPE::OUTPUT:
    case IBIS_MODEL_TYPE::IO:
    case IBIS_MODEL_TYPE::THREE_STATE:
    case IBIS_MODEL_TYPE::OPEN_DRAIN:
    case IBIS_MODEL_TYPE::IO_OPEN_DRAIN:
    case IBIS_MODEL_TYPE::OPEN_SINK:
    case IBIS_MODEL_TYPE::IO_OPEN_SINK:
    case IBIS_MODEL_TYPE::OPEN_SOURCE:
    case IBIS_MODEL_TYPE::IO_OPEN_SOURCE:
    case IBIS_MODEL_TYPE::OUTPUT_ECL:
    case IBIS_MODEL_TYPE::IO_ECL:
    case IBIS_MODEL_TYPE::THREE_STATE_ECL:
    {
        std::string result;
        std::string tmp;

        result = "\n*Driver model generated by Kicad using Ibis data. ";
        result += "\n*Component: ";

        if( m_parent )
        {
            result += m_parent->m_name;
        }
        result += "\n*Manufacturer: ";

        if( m_parent )
        {
            result += m_parent->m_manufacturer;
        }
        result += "\n*Pin number: ";
        result += m_pinNumber;
        result += "\n*Signal name: ";
        result += m_signalName;
        result += "\n*Model: ";
        result += aModel.m_name;
        result += "\n.SUBCKT ";
        result += aName;
        result += " GND PIN \n";
        result += "\n";

        result += "RPIN 1 PIN ";
        result += doubleToString( R_pin->value[aParasitics] );
        result += "\n";
        result += "LPIN DIE0 1 ";
        result += doubleToString( L_pin->value[aParasitics] );
        result += "\n";
        result += "CPIN PIN GND ";
        result += doubleToString( C_pin->value[aParasitics] );
        result += "\n";

        std::vector<std::pair<IbisWaveform*, IbisWaveform*>> wfPairs = aModel.waveformPairs();

        if( wfPairs.size() < 1 || aAccuracy <= KIBIS_ACCURACY::LEVEL_0 )
        {
            if( aAccuracy > KIBIS_ACCURACY::LEVEL_0 )
            {
                Report( _( "Model has no waveform pair, using [Ramp] instead, poor accuracy" ),
                        RPT_SEVERITY_INFO );
            }
            getKuKdNoWaveform( aModel, aWave, aSupply );
        }
        else if( wfPairs.size() == 1 || aAccuracy <= KIBIS_ACCURACY::LEVEL_1 )
        {
            getKuKdOneWaveform( aModel, wfPairs.at( 0 ), aWave, aSupply, aParasitics );
        }
        else
        {
            if( wfPairs.size() > 2 || aAccuracy <= KIBIS_ACCURACY::LEVEL_2 )
            {
                Report( _( "Model has more than 2 waveform pairs, using the first two." ),
                        RPT_SEVERITY_WARNING );
            }
            getKuKdTwoWaveforms( aModel, wfPairs.at( 0 ), wfPairs.at( 1 ), aWave, aSupply, aParasitics );
        }

        result += "Vku KU GND pwl ( ";
        for( int i = 0; i < m_t.size(); i++ )
        {
            result += doubleToString( m_t.at( i ) );
            result += " ";
            result += doubleToString( m_Ku.at( i ) );
            result += " ";
        }
        result += ") \n";


        result += "Vkd KD GND pwl ( ";
        for( int i = 0; i < m_t.size(); i++ )
        {
            result += doubleToString( m_t.at( i ) );
            result += " ";
            result += doubleToString( m_Kd.at( i ) );
            result += " ";
        }

        result += ") \n";

        result += aModel.SpiceDie( aSupply, aParasitics, 0 );

        result += "\n.ENDS DRIVER\n\n";

        *aDest += result;
        break;
    }
    default: Report( _( "Invalid model type for a driver." ), RPT_SEVERITY_ERROR ); status = false;
    }

    return status;
}


bool KIBIS_PIN::writeSpiceDevice( std::string* aDest, std::string aName, KIBIS_MODEL& aModel,
                                  IBIS_CORNER aSupply, IBIS_CORNER aParasitics )
{
    bool status = true;

    switch( aModel.m_type )
    {
    case IBIS_MODEL_TYPE::INPUT:
    case IBIS_MODEL_TYPE::IO:
    case IBIS_MODEL_TYPE::IO_OPEN_DRAIN:
    case IBIS_MODEL_TYPE::IO_OPEN_SINK:
    case IBIS_MODEL_TYPE::IO_OPEN_SOURCE:
    case IBIS_MODEL_TYPE::IO_ECL:
    {
        std::string result;
        std::string tmp;

        result += "\n";
        result = "*Device model generated by Kicad using Ibis data.";
        result += "\n.SUBCKT ";
        result += aName;
        result += " GND PIN\n";
        result += "\n";
        result += "\n";
        result += "RPIN 1 PIN ";
        result += doubleToString( R_pin->value[aParasitics] );
        result += "\n";
        result += "LPIN DIE 1 ";
        result += doubleToString( L_pin->value[aParasitics] );
        result += "\n";
        result += "CPIN PIN GND ";
        result += doubleToString( C_pin->value[aParasitics] );
        result += "\n";


        result += "Vku KU GND pwl ( 0 0 )\n";
        result += "Vkd KD GND pwl ( 0 0 )\n";

        result += aModel.SpiceDie( aSupply, aParasitics, 0 );

        result += "\n.ENDS DRIVER\n\n";

        *aDest = result;
        break;
    }
    default: Report( _( "Invalid model type for a device" ), RPT_SEVERITY_ERROR ); status = false;
    }

    return status;
}

bool KIBIS_PIN::writeSpiceDiffDriver( std::string* aDest, std::string aName, KIBIS_MODEL& aModel,
                                      IBIS_CORNER aSupply, IBIS_CORNER aParasitics,
                                      KIBIS_ACCURACY aAccuracy, KIBIS_WAVEFORM* aWave )
{
    bool status = true;

    std::string result;
    result = "\n*Differential driver model generated by Kicad using Ibis data. ";
    result += "\n*Component: ";

    if( m_parent )
    {
        result += m_parent->m_name;
    }
    result += "\n*Manufacturer: ";

    if( m_parent )
    {
        result += m_parent->m_manufacturer;
    }

    result += "\n.SUBCKT ";
    result += aName;
    result += " GND PIN_P PIN_N\n";
    result += "\n";

    status &= writeSpiceDriver( &result, aName + "_P", aModel, aSupply, aParasitics, aAccuracy,
                                aWave );
    aWave->inverted = !aWave->inverted;
    status &= writeSpiceDriver( &result, aName + "_N", aModel, aSupply, aParasitics, aAccuracy,
                                aWave );
    aWave->inverted = !aWave->inverted;


    result += "\n";
    result += "x1 GND PIN_P " + aName + "_P \n";
    result += "x2 GND PIN_N " + aName + "_N \n";
    result += "\n";

    result += "\n.ENDS " + aName + "\n\n";

    if( status )
    {
        *aDest += result;
    }

    return status;
}
