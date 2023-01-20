/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 1992-2010 Jean-Pierre Charras <jp.charras at wanadoo.fr>
 * Copyright (C) 2010 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 1992-2023 KiCad Developers, see AUTHORS.txt for contributors.
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

/**
 * @file aperture_macro.h
 */

#ifndef APERTURE_MACRO_H
#define APERTURE_MACRO_H


#include <vector>
#include <set>

#include <am_param.h>
#include <am_primitive.h>

class SHAPE_POLY_SET;

/*
 *  An aperture macro defines a complex shape and is a list of aperture primitives.
 *  Each aperture primitive defines a simple shape (circle, rect, regular polygon...)
 *  Inside a given aperture primitive, a fixed list of parameters defines info
 *  about the shape: size, thickness, number of vertex ...
 *
 *  Each parameter can be an immediate value or a deferred value.
 *  When value is deferred, it is defined when the aperture macro is instanced by
 *  an ADD macro command
 *  Note also a deferred parameter can be defined in aperture macro,
 *  but outside aperture primitives. Example
 *  %AMRECTHERM*
 *  $4=$3/2*    parameter $4 is half value of parameter $3
 *  21,1,$1-$3,$2-$3,0-$1/2-$4,0-$2/2-$4,0*
 *  For the aperture primitive, parameters $1 to $3 will be defined in ADD command,
 *  and $4 is defined inside the macro
 *
 *  Each basic shape can be a positive shape or a negative shape.
 *  a negative shape is "local" to the whole shape.
 *  It must be seen like a hole in the shape, and not like a standard negative object.
 */


/**
 * Support the "aperture macro" defined within standard RS274X.
 */
class APERTURE_MACRO
{
public:
    /**
     * Usually, parameters are defined inside the aperture primitive using immediate mode or
     * deferred mode.
     *
     * In deferred mode the value is defined in a DCODE that want to use the aperture macro.
     * Some parameters are defined outside the aperture primitive and are local to the aperture
     * macro.
     *
     * @return the value of a deferred parameter defined inside the aperture macro.
     * @param aDcode is the D_CODE that uses this aperture macro and define deferred parameters.
     * @param aParamId is the param id (defined by $3 or $5 ..) to evaluate.
     */
    double GetLocalParam( const D_CODE* aDcode, unsigned aParamId ) const;


    /**
     * Calculate the primitive shape for flashed items.
     *
     * When an item is flashed, this is the shape of the item.
     *
     * @param aParent is the parent #GERBER_DRAW_ITEM which is actually drawn.
     * @return the shape of the item.
     */
    SHAPE_POLY_SET* GetApertureMacroShape( const GERBER_DRAW_ITEM* aParent,
                                           const VECTOR2I&         aShapePos );

    /**
     * The name of the aperture macro as defined like %AMVB_RECTANGLE* (name is VB_RECTANGLE)
     */
     wxString      m_AmName;

    /**
     * A sequence of AM_PRIMITIVEs
     */
    std::vector<AM_PRIMITIVE> m_PrimitivesList;

    /*  A deferred parameter can be defined in aperture macro,
     *  but outside aperture primitives. Example
     *  %AMRECTHERM*
     *  $4=$3/2*    parameter $4 is half value of parameter $3
     * m_localparamStack handle a list of local deferred parameters
     */
    AM_PARAMS m_LocalParamStack;

private:
    SHAPE_POLY_SET m_shape;         ///< The shape of the item, calculated by GetApertureMacroShape
};


/**
 * Used by std:set<APERTURE_MACRO> instantiation which uses APERTURE_MACRO.name as its key.
 */
struct APERTURE_MACRO_less_than
{
    // a "less than" test on two APERTURE_MACROs (.name wxStrings)
    bool operator()( const APERTURE_MACRO& am1, const APERTURE_MACRO& am2 ) const
    {
        return am1.m_AmName.Cmp( am2.m_AmName ) < 0;  // case specific wxString compare
    }
};


/**
 * A sorted collection of APERTURE_MACROS whose key is the name field in the APERTURE_MACRO.
 */
typedef std::set<APERTURE_MACRO, APERTURE_MACRO_less_than> APERTURE_MACRO_SET;
typedef std::pair<APERTURE_MACRO_SET::iterator, bool>      APERTURE_MACRO_SET_PAIR;


#endif  // ifndef APERTURE_MACRO_H
