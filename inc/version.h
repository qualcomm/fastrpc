// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERSION_H
#define VERSION_H
/*===========================================================================

FILE:  version.h

GENERAL DESCRIPTION:
        Definitions for versioning
===========================================================================*/
#if !defined(VERSION_CL)
#define VERSION_CL "?"
#endif

#if !defined(VERSION_PROD)
#define VERSION_PROD "unknown"
#endif

#if !defined(VERSION_BRANCH)
#define VERSION_BRANCH "?"
#endif

#if !defined(VERSION_NUM)
#define VERSION_NUM "?.?.?.?"
#endif

#define VERSION_STRING                                                        \
	VERSION_PROD " " VERSION_NUM " "                                      \
		     "(br=" VERSION_BRANCH "; cl=" VERSION_CL ")"

/*
=======================================================================
MACROS DOCUMENTATION
=======================================================================

VERSION_MAJOR

Description:
        Defines the major release number of the version.

Comments:
    It has to be a valid numerical value
=======================================================================

VERSION_MINOR

Description:
        Defines the minor release number of the version.

Comments:
    It has to be a valid numerical value
=======================================================================

VERSION_MAINT

Description:
        Defines the maintenance release of the version.

Comments:
    It has to be a valid numerical value
=======================================================================

VERSION_BUILD

Description:
        Defines the build ID of the version.

Comments:
    It has to be a valid numerical value
=======================================================================

VERSION_STRING

Description:
        Defines the version string that specifies the version number.

Definition:

   #define VERSION_STRING "a.b.c.d (name=value;name=value;...)"
        where a=major release number
              b=minor release number
              c=maintenance release number
              d=build number

        name=value pair provides additional information about the build.
        Example:
        patch/feature=comma separated list of features/patches that have been
installed. br=p4 branch that was used for the build cl=p4 change list number
        machine=hostname of the machine that was used for the build.

Comments:

=======================================================================
*/

#endif // VERSION_H
