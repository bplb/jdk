/*
 * Copyright (c) 2002, 2025, Oracle and/or its affiliates. All rights reserved.
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

package nsk.jdi.ThreadReference.ownedMonitors;

import nsk.share.*;
import nsk.share.jpda.*;
import nsk.share.jdi.*;

/**
 * This is a debuggee class.
 */
public class ownedmonitors002t {
    static Thread testThread = null;

    public static void main(String args[]) {
        ArgumentHandler argHandler = new ArgumentHandler(args);

        Log log = argHandler.createDebugeeLog();
        IOPipe pipe = argHandler.createDebugeeIOPipe();

        testThread = Thread.currentThread();
        testThread.setName(ownedmonitors002.DEBUGGEE_THRNAME);

        // Now the debuggee is ready for testing
        pipe.println(ownedmonitors002.COMMAND_READY);

        String cmd = pipe.readln();
        if (!cmd.equals(ownedmonitors002.COMMAND_QUIT)) {
            log.complain("TEST BUG: unknown debugger command: "
                + cmd);
            System.exit(Consts.JCK_STATUS_BASE +
                Consts.TEST_FAILED);
        }
        System.exit(Consts.JCK_STATUS_BASE + Consts.TEST_PASSED);
    }
}
