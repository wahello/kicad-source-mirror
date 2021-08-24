/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2012 Torsten Hueter, torstenhtr <at> gmx.de
 * Copyright (C) 2012 Kicad Developers, see change_log.txt for contributors.
 *
 * Macro definitions
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

#ifndef DEFINITIONS_H_
#define DEFINITIONS_H_

/// Swap the variables if a condition is met.
#define SWAP( varA, condition, varB )   \
    assert( typeid( varA ).hash_code() == typeid( varB ).hash_code() ); \
    \
    if( varA condition varB )           \
    {                                   \
        decltype( varA ) tmp = varA;    \
        varA = varB;                    \
        varB = tmp;                     \
    }

namespace KIGFX
{
/**
 * RENDER_TARGET: Possible rendering targets
 */
enum RENDER_TARGET
{
    TARGET_CACHED = 0,      ///< Main rendering target (cached)
    TARGET_NONCACHED,       ///< Auxiliary rendering target (noncached)
    TARGET_OVERLAY,         ///< Items that may change while the view stays the same (noncached)
    TARGET_TEMP,            ///< Temporary target for drawing in separate layer
    TARGETS_NUMBER          ///< Number of available rendering targets
};
} // namespace KIGFX

#endif /* DEFINITIONS_H_ */
