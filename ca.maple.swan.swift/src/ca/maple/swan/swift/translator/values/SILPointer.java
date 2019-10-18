//===--- SILPointer.java -------------------------------------------------===//
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

package ca.maple.swan.swift.translator.values;

import ca.maple.swan.swift.translator.SILInstructionContext;
import com.ibm.wala.cast.tree.CAstNode;

/*
 * We can keep track of a pointer's underlying value using this class.
 * This class does not have to have an underlying value, so we need
 * to check if it has an underlying value ( hasValue() ). This is so
 * that instructions that create pointers such as alloc_stack can be
 * handled explicitly. Typically any type starting with $*.

 * We want as much as possibly 1:1 representation type wise. So if
 * an instruction expects a pointer as an operand, we have to make sure
 * it exists as a SILPointer in our table.
 */

public class SILPointer extends SILValue {

    private SILValue pointsTo;
    private boolean hasValue = true;

    public SILPointer(String name, String type, SILInstructionContext C, SILValue pointsTo) {
        super(name, type, C);
        this.pointsTo = pointsTo;
    }

    public SILPointer(String name, String type, SILInstructionContext C) {
        this(name, type, C, null);
        this.hasValue = false;
    }

    public SILValue dereference() {
        if (pointsTo == null) { // This isn't ideal;
            return this;
        }
        if (pointsTo instanceof SILPointer) {
            return ((SILPointer) pointsTo).dereference();
        }
        return pointsTo;
    }

    public boolean hasValue() {
        return this.hasValue;
    }

    public CAstNode getUnderlyingVar() {
        if (pointsTo == null) { // This isn't ideal;
            return this.varNode;
        }
        return pointsTo.getVarNode();
    }

    @Override
    public CAstNode getVarNode() {
        return getUnderlyingVar();
    }

    @Override
    public CAstNode copy(String ResultName, String ResultType) {
        C.valueTable.addValue(this.copyPointer(ResultName, ResultType));
        return null;
    }

    public SILPointer copyPointer(String newName, String newType) {
        // If the pointer has no underlying value, it will conveniently
        // point the new pointer to this pointer.
        return new SILPointer(newName, newType, C, dereference());
    }

    public void replaceUnderlyingVar(SILValue to)  {
        C.valueTable.replaceValue(to.name, to);
        pointsTo = to;
    }
}