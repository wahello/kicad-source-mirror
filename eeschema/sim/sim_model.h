/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2022 Mikolaj Wielgus
 * Copyright (C) 2022 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * https://www.gnu.org/licenses/gpl-3.0.html
 * or you may search the http://www.gnu.org website for the version 3 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef SIM_MODEL_H
#define SIM_MODEL_H

#include <sim/spice_grammar.h>
#include <enum_vector.h>
#include <sch_field.h>
#include <lib_field.h>
#include <wx/string.h>
#include <map>
#include <stdexcept>

class SIM_LIBRARY;


namespace SIM_MODEL_GRAMMAR
{
    using namespace SPICE_GRAMMAR;


    struct pinNumber : sor<digits, one<'X'>> {};
    struct pinSequence : seq<opt<pinNumber,
                                 star<sep,
                                      pinNumber>>> {};

    struct pinSequenceGrammar : must<opt<sep>,
                                     pinSequence,
                                     opt<sep>,
                                     eof> {};

    template <NOTATION Notation>
    struct paramValuePairsGrammar : must<opt<sep>,
                                         paramValuePairs<Notation>,
                                         opt<sep>,
                                         eof> {};
}


class SIM_MODEL
{
public:
    static constexpr auto REFERENCE_FIELD = "Reference";
    static constexpr auto VALUE_FIELD = "Value";

    static constexpr auto DEVICE_TYPE_FIELD = "Model_Device";
    static constexpr auto TYPE_FIELD = "Model_Type";
    static constexpr auto PINS_FIELD = "Model_Pins";
    static constexpr auto PARAMS_FIELD = "Model_Params";


    DEFINE_ENUM_CLASS_WITH_ITERATOR( DEVICE_TYPE, 
        NONE,

        R,
        C,
        L,
        TLINE,
        SW,

        D,
        NPN,
        PNP,

        NJFET,
        PJFET,

        NMES,
        PMES,

        NMOS,
        PMOS,

        V,
        I,

        SUBCKT,
        XSPICE,
        SPICE
    )

    struct DEVICE_INFO
    {
        wxString fieldValue;
        wxString description;
    };


    DEFINE_ENUM_CLASS_WITH_ITERATOR( TYPE,
        NONE,

        R,
        R_ADV,
        R_BEHAVIORAL,

        C,
        C_ADV,
        C_BEHAVIORAL,

        L,
        L_ADV,
        L_BEHAVIORAL,

        TLINE_LOSSY,
        TLINE_LOSSLESS,
        TLINE_URC,
        TLINE_KSPICE,

        SW_V,
        SW_I,

        D,

        NPN_GUMMELPOON,
        PNP_GUMMELPOON,
        NPN_VBIC,
        PNP_VBIC,
        //NPN_MEXTRAM,
        //PNP_MEXTRAM,
        NPN_HICUML2,
        PNP_HICUML2,
        //NPN_HICUM_L0,
        //PNP_HICUM_L0,

        NJFET_SHICHMANHODGES,
        PJFET_SHICHMANHODGES,

        NJFET_PARKERSKELLERN,
        PJFET_PARKERSKELLERN,


        NMES_STATZ,
        PMES_STATZ,

        NMES_YTTERDAL,
        PMES_YTTERDAL,

        NMES_HFET1,
        PMES_HFET1,

        NMES_HFET2,
        PMES_HFET2,


        NMOS_MOS1,
        PMOS_MOS1,

        NMOS_MOS2,
        PMOS_MOS2,

        NMOS_MOS3,
        PMOS_MOS3,

        NMOS_BSIM1,
        PMOS_BSIM1,

        NMOS_BSIM2,
        PMOS_BSIM2,

        NMOS_MOS6,
        PMOS_MOS6,

        NMOS_MOS9,
        PMOS_MOS9,

        NMOS_BSIM3,
        PMOS_BSIM3,

        NMOS_B4SOI,
        PMOS_B4SOI,

        NMOS_BSIM4,
        PMOS_BSIM4,

        //NMOS_EKV2_6,
        //PMOS_EKV2_6,

        //NMOS_PSP,
        //PMOS_PSP,

        NMOS_B3SOIFD,
        PMOS_B3SOIFD,

        NMOS_B3SOIDD,
        PMOS_B3SOIDD,

        NMOS_B3SOIPD,
        PMOS_B3SOIPD,

        //NMOS_STAG,
        //PMOS_STAG,

        NMOS_HISIM2,
        PMOS_HISIM2,

        NMOS_HISIMHV1,
        PMOS_HISIMHV1,

        NMOS_HISIMHV2,
        PMOS_HISIMHV2,


        V_DC,
        V_SIN,
        V_PULSE,
        V_EXP,
        V_SFAM,
        V_SFFM,
        V_PWL,
        V_WHITENOISE,
        V_PINKNOISE,
        V_BURSTNOISE,
        V_RANDUNIFORM,
        V_RANDNORMAL,
        V_RANDEXP,
        V_RANDPOISSON,
        V_BEHAVIORAL,

        I_DC,
        I_SIN,
        I_PULSE,
        I_EXP,
        I_SFAM,
        I_SFFM,
        I_PWL,
        I_WHITENOISE,
        I_PINKNOISE,
        I_BURSTNOISE,
        I_RANDUNIFORM,
        I_RANDNORMAL,
        I_RANDEXP,
        I_RANDPOISSON,
        I_BEHAVIORAL,


        SUBCKT,
        XSPICE,
        SPICE
    )

    struct INFO
    {
        DEVICE_TYPE deviceType;
        wxString fieldValue;
        wxString description;
    };


    struct SPICE_INFO
    {
        wxString primitive;
        wxString modelType = "";
        wxString inlineTypeString = "";
        int level = 0;
        bool hasExpression = false;
        wxString version = "";
    };


    struct PIN
    {
        static constexpr auto NOT_CONNECTED = 0;

        const wxString name;
        int symbolPinNumber;
    };


    struct PARAM
    {
        enum class DIR
        {
            IN,
            OUT,
            INOUT
        };

        enum class CATEGORY
        {
            PRINCIPAL,
            DC,
            CAPACITANCE,
            TEMPERATURE,
            NOISE,
            DISTRIBUTED_QUANTITIES,
            GEOMETRY,
            LIMITING_VALUES,
            ADVANCED,
            FLAGS,
            INITIAL_CONDITIONS,
            SUPERFLUOUS
        };

        struct FLAGS {}; // Legacy.

        struct INFO
        {
            wxString name;
            unsigned int id = 0; // Legacy.
            DIR dir = DIR::INOUT;
            SIM_VALUE::TYPE type = SIM_VALUE::TYPE::FLOAT;
            FLAGS flags = {}; // Legacy
            wxString unit = "";
            CATEGORY category = CATEGORY::PRINCIPAL;
            wxString defaultValue = "";
            wxString defaultValueOfOtherVariant = ""; // Legacy.
            wxString description = "";
            bool isInstanceParam = false;
        };

        std::unique_ptr<SIM_VALUE> value;
        const INFO& info;
        bool isOtherVariant = false; // Legacy.
        
        PARAM( const INFO& aInfo, bool aIsOtherVariant = false )
            : value( SIM_VALUE::Create( aInfo.type ) ),
              info( aInfo ),
              isOtherVariant( aIsOtherVariant )
        {}
    };


    static DEVICE_INFO DeviceTypeInfo( DEVICE_TYPE aDeviceType );
    static INFO TypeInfo( TYPE aType );
    static SPICE_INFO SpiceInfo( TYPE aType );


    static TYPE ReadTypeFromSpiceCode( const std::string& aSpiceCode );

    template <typename T>
    static TYPE ReadTypeFromFields( const std::vector<T>& aFields );


    static std::unique_ptr<SIM_MODEL> Create( TYPE aType, int aSymbolPinCount = 0 );
    static std::unique_ptr<SIM_MODEL> Create( const std::string& aSpiceCode );

    template <typename T>
    static std::unique_ptr<SIM_MODEL> Create( const SIM_MODEL& aBaseModel, int aSymbolPinCount,
                                              const std::vector<T>& aFields );

    template <typename T>
    static std::unique_ptr<SIM_MODEL> Create( int aSymbolPinCount, const std::vector<T>& aFields );

    template <typename T>
    static wxString GetFieldValue( const std::vector<T>* aFields, const wxString& aFieldName );

    template <typename T>
    static void SetFieldValue( std::vector<T>& aFields, const wxString& aFieldName,
                               const wxString& aValue );


    // Move semantics.
    // Rule of five.
    virtual ~SIM_MODEL() = default;
    SIM_MODEL() = delete;
    SIM_MODEL( const SIM_MODEL& aOther ) = delete;
    SIM_MODEL( SIM_MODEL&& aOther ) = default;
    SIM_MODEL& operator=(SIM_MODEL&& aOther ) = delete;


    virtual bool ReadSpiceCode( const std::string& aSpiceCode );

    template <typename T>
    void ReadDataFields( int aSymbolPinCount, const std::vector<T>* aFields );

    // C++ doesn't allow virtual template methods, so we do this:
    virtual void ReadDataSchFields( int aSymbolPinCount, const std::vector<SCH_FIELD>* aFields );
    virtual void ReadDataLibFields( int aSymbolPinCount, const std::vector<LIB_FIELD>* aFields );


    template <typename T>
    void WriteFields( std::vector<T>& aFields );

    // C++ doesn't allow virtual template methods, so we do this:
    virtual void WriteDataSchFields( std::vector<SCH_FIELD>& aFields );
    virtual void WriteDataLibFields( std::vector<LIB_FIELD>& aFields );


    virtual bool HasToIncludeSpiceLibrary() const { return GetBaseModel() && !HasOverrides(); }

    virtual wxString GenerateSpiceModelLine( const wxString& aModelName ) const;

    virtual wxString GenerateSpiceItemName( const wxString& aRefName ) const;
    wxString GenerateSpiceItemLine( const wxString& aRefName, const wxString& aModelName ) const;
    virtual wxString GenerateSpiceItemLine( const wxString& aRefName,
                                            const wxString& aModelName,
                                            const std::vector<wxString>& aPinNetNames ) const;

    virtual wxString GenerateSpiceTuningLine( const wxString& aSymbol ) const;

    virtual wxString GenerateSpicePreview( const wxString& aModelName ) const;

    SPICE_INFO GetSpiceInfo() const;
    virtual std::vector<wxString> GenerateSpiceCurrentNames( const wxString& aRefName ) const;

    bool ParsePinsField( int aSymbolPinCount, const wxString& aPinsField );

    void AddPin( const PIN& aPin );
    int FindModelPinNumber( int aSymbolPinNumber );
    void AddParam( const PARAM::INFO& aInfo, bool aIsOtherVariant = false );

    DEVICE_TYPE GetDeviceType() const { return TypeInfo( GetType() ).deviceType; }
    TYPE GetType() const { return m_type; }

    const SIM_MODEL* GetBaseModel() const { return m_baseModel; }
    virtual void SetBaseModel( const SIM_MODEL& aBaseModel ) { m_baseModel = &aBaseModel; }

    int GetPinCount() const { return static_cast<int>( m_pins.size() ); }
    const PIN& GetPin( int aIndex ) const { return m_pins.at( aIndex ); }

    void SetPinSymbolPinNumber( int aIndex, int aSymbolPinNumber )
    {
        m_pins.at( aIndex ).symbolPinNumber = aSymbolPinNumber;
    }


    int GetParamCount() const { return static_cast<int>( m_params.size() ); }
    const PARAM& GetParam( int aParamIndex ) const; // Return base parameter unless it's overridden.
    const PARAM& GetUnderlyingParam( int aParamIndex ) const; // Return the actual parameter.
    const PARAM& GetBaseParam( int aParamIndex ) const; // Always return base parameter if it exists.
    virtual bool SetParamValue( int aParamIndex, const wxString& aValue,
                                SIM_VALUE_GRAMMAR::NOTATION aNotation
                                    = SIM_VALUE_GRAMMAR::NOTATION::SI );

    bool HasOverrides() const;
    bool HasNonPrincipalOverrides() const;

    // Can modifying a model parameter also modify other parameters?
    virtual bool HasAutofill() const { return false; }

protected:
    SIM_MODEL( TYPE aType );

    wxString m_spiceCode;

private:
    static std::unique_ptr<SIM_MODEL> create( TYPE aType );
    static TYPE readTypeFromSpiceTypeString( const std::string& aTypeString );


    template <typename T>
    void doReadDataFields( int aSymbolPinCount, const std::vector<T>* aFields );

    template <typename T>
    void doWriteFields( std::vector<T>& aFields );


    virtual std::vector<wxString> getPinNames() const { return {}; }

    wxString generateDeviceTypeField() const;
    wxString generateTypeField() const;

    wxString generatePinsField() const;

    
    wxString generateParamsField( const wxString& aPairSeparator ) const;
    bool parseParamsField( const wxString& aParamsField );

    // TODO: Rename.
    virtual bool setParamFromSpiceCode( const wxString& aParamName, const wxString& aParamValue,
                                        SIM_VALUE_GRAMMAR::NOTATION aNotation
                                            = SIM_VALUE_GRAMMAR::NOTATION::SPICE );


    const SIM_MODEL* m_baseModel;

    const TYPE m_type;
    std::vector<PIN> m_pins;
    std::vector<PARAM> m_params;
};

#endif // SIM_MODEL_H
