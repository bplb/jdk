/*
 * Copyright (c) 2017, 2025, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */


/*
 * @test
 * @key stress randomness
 *
 * @summary converted from VM Testbase vm/gc/compact/Humongous_InternedStrings.
 * VM Testbase keywords: [gc, stress, stressopt, feature_g1, nonconcurrent]
 * VM Testbase readme:
 * Refer to vm/gc/compact/README for more information about the test.
 * The source for the test can be found in vm/gc/compact/Compact.java.
 * This testcase uses interned strings for both first and second phases
 * and multiple threads.
 *
 * @requires os.maxMemory > 3G
 * @library /vmTestbase
 *          /test/lib
 * @run main/othervm
 *      -Xmx2G
 *      -XX:-UseGCOverheadLimit
 *      vm.gc.compact.Compact
 *      -gp interned(randomString)
 *      -gp1 random(arrays)
 *      -ms high
 *      -size2 1000000
 */

