##############################################################################
# Copyright (c) 2013, Lawrence Livermore National Security, LLC.
# Produced at the Lawrence Livermore National Laboratory.
# 
# This file is part of Spack.
# Written by Todd Gamblin, tgamblin@llnl.gov, All rights reserved.
# LLNL-CODE-647188
# 
# For details, see https://scalability-llnl.github.io/spack
# Please also see the LICENSE file for our notice and the LGPL.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License (as published by
# the Free Software Foundation) version 2.1 dated February 1999.
# 
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the terms and
# conditions of the GNU General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
##############################################################################
from spack import *

class Libelf(Package):
    homepage = "http://www.mr511.de/software/english.html"
    url      = "http://www.mr511.de/software/libelf-0.8.13.tar.gz"

    versions =   {'0.8.13' : '4136d7b4c04df68b686570afa26988ac',
                  '0.8.12' : 'e21f8273d9f5f6d43a59878dc274fec7',
                  '0.8.10' : '9db4d36c283d9790d8fa7df1f4d7b4d9' }

    def install(self, spec, prefix):
        configure("--prefix=%s" % prefix,
                  "--enable-shared",
                  "--disable-dependency-tracking",
                  "--disable-debug")
        make()

        # The mkdir commands in libelf's intsall can fail in parallel
        make("install", parallel=False)