//===--- ReflexiveMapper.java ----------------------------------------===//
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

package ca.maple.swan.swift.tree;

import com.ibm.wala.cast.ir.translator.AbstractCodeEntity;
import com.ibm.wala.cast.tree.CAstNode;

import java.util.ArrayList;

/*
 * This class maps a given CAstEntity's AST nodes to themselves since WALA requires this. There are certain nodes
 * that won't be mapped to themselves for nuanced reasons.
 */
public class ReflexiveMapper {

    // Don't map nodes of these types to themselves.
    private static ArrayList<Integer> blackListedNodes = new ArrayList<Integer>();
    static {
        blackListedNodes.add(CAstNode.BINARY_EXPR);
    }

    static public void mapEntity(AbstractCodeEntity entity) {
       CAstNode root = entity.getAST();
       entity.setGotoTarget(root, root);
       map(entity, root);
    }

    static private void map(AbstractCodeEntity entity, CAstNode node) {
       if (node.getChildren().isEmpty()) {
         return;
       }
       for (CAstNode n : node.getChildren()) {
           if (!entity.getControlFlow().isMapped(n) && !blackListedNodes.contains(n.getKind())) {
               entity.setGotoTarget(n, n);
           }
           map(entity, n);
       }
    }
}
