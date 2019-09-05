//===--- SILInstructionContext.java --------------------------------------===//
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

package ca.maple.swan.swift.translator;

import ca.maple.swan.swift.translator.values.SILValueTable;
import com.ibm.wala.cast.ir.translator.AbstractCodeEntity;
import com.ibm.wala.cast.tree.CAstNode;

import java.util.ArrayList;
import java.util.HashMap;

/*
 * This class contains any context needed for any node to be translated.
 */
public class SILInstructionContext {
    public final AbstractCodeEntity parent;
    public final ArrayList<AbstractCodeEntity> allEntities;
    public final SILValueTable valueTable;
    public ArrayList<CAstNode> instructions;
    HashMap<Integer, ArrayList<CAstNode>> danglingGOTOs;
    ArrayList<ArrayList<CAstNode>> blocks;
    public final CAstNode currentFunction;

    public SILInstructionContext(AbstractCodeEntity parent, ArrayList<AbstractCodeEntity> allEntities, CAstNode currentFunction) {
        this.parent = parent;
        this.allEntities = allEntities;
        valueTable =  new SILValueTable();
        danglingGOTOs = new HashMap<>();
        blocks = new ArrayList<>();
        this.currentFunction = currentFunction;
    }

    public void clearDanglingGOTOs() {
        danglingGOTOs = new HashMap<>();
    }

    public void clearInstructions() {
        instructions = new ArrayList<>();
    }

    public void clearBlocks() {
        blocks = new ArrayList<>();
    }
}
