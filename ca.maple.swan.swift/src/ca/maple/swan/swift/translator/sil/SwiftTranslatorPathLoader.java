//===--- SwiftTranslatorPathLoader.java ----------------------------------===//
//
// This source file is part of the SWAN open source project
//
// Copyright (c) 2019 Maple @ University of Alberta
// All rights reserved. This program and the accompanying materials (unless
// otherwise specified by a license inside of the accompanying material)
// are made available under the terms of the Eclipse Public License v2.0
// which accompanies this distribution, and is available at
// http://www.eclipse.org/legal/epl-v20.html
//
//===---------------------------------------------------------------------===//

package ca.maple.swan.swift.translator.sil;

/*
 * This [janky] class is needed for now due to the fact that we are using a
 * shared library generated by the C++ side.
 */

public class SwiftTranslatorPathLoader {

    public static void load() {
        String libName = "/lib/libswiftWala";

        String SWANDir = "";
        try {
            SWANDir = System.getenv("PATH_TO_SWAN");
            if (SWANDir.equals("null")) {
                throw new Exception();
            }
        } catch (Exception e) {
            System.err.println("Error: PATH_TO_SWAN path not set! Exiting...");
            e.printStackTrace();
            System.exit(1);
        }

        try {
            System.load(SWANDir + libName + ".dylib");
        } catch (UnsatisfiedLinkError dylibException) {
            System.err.println("Could not find shared library!");
            dylibException.printStackTrace();
        }
    }
}