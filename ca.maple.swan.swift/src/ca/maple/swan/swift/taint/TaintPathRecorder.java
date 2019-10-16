//===--- TaintPathRecorder.java ------------------------------------------===//
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

package ca.maple.swan.swift.taint;

import com.ibm.wala.cast.loader.AstMethod;
import com.ibm.wala.cast.tree.CAstSourcePositionMap;
import com.ibm.wala.classLoader.IMethod;
import com.ibm.wala.ipa.slicer.NormalReturnCaller;
import com.ibm.wala.ipa.slicer.NormalStatement;
import com.ibm.wala.ipa.slicer.ParamCaller;
import com.ibm.wala.ipa.slicer.Statement;
import com.ibm.wala.util.graph.Graph;
import com.ibm.wala.util.graph.traverse.BFSPathFinder;

import java.util.*;

public class TaintPathRecorder {

    private static HashMap<Statement, LinkedList<Statement>> sourcesAndTargets = new HashMap<>();

    public static void recordPath(ArrayList<Statement> sources, Statement target) {
        for (Statement src : sources) {
            sourcesAndTargets.computeIfAbsent(src, (k) ->
                    new LinkedList<>()
            );
            sourcesAndTargets.get(src).add(target);
        }
    }

    public static Set<Statement> getSources() {
        return sourcesAndTargets.keySet();
    }

    public static List<Statement> getTargets(Statement src) {
        return sourcesAndTargets.get(src);
    }

    public static void clear() {
        sourcesAndTargets.clear();
    }

    public static List<CAstSourcePositionMap.Position> getPositionsFromStatements(List<Statement> statements) {
        List<CAstSourcePositionMap.Position> path = new ArrayList<>();
        for (Statement s : statements) {
            CAstSourcePositionMap.Position p = null;
            IMethod m = s.getNode().getMethod();
            boolean ast = m instanceof AstMethod;
            if (ast) {
                switch (s.getKind()) {
                    case NORMAL:
                        p = ((AstMethod) m).getSourcePosition(((NormalStatement) s).getInstructionIndex());
                        break;
                    case PHI:
                        break;
                    case PI:
                        break;
                    case CATCH:
                        break;
                    case PARAM_CALLER:
                        p = ((AstMethod) m).getSourcePosition(((ParamCaller) s).getInstructionIndex());
                        break;
                    case PARAM_CALLEE:
                        p = ((AstMethod) m).getSourcePosition();
                        break;
                    case NORMAL_RET_CALLER:
                        p = ((AstMethod) m).getSourcePosition(((NormalReturnCaller)s).getInstructionIndex());
                        break;
                    case NORMAL_RET_CALLEE:
                        break;
                    case EXC_RET_CALLER:
                        break;
                    case EXC_RET_CALLEE:
                        break;
                    case HEAP_PARAM_CALLER:
                        break;
                    case HEAP_PARAM_CALLEE:
                        break;
                    case HEAP_RET_CALLER:
                        break;
                    case HEAP_RET_CALLEE:
                        break;
                    case METHOD_ENTRY:
                        break;
                    case METHOD_EXIT:
                        break;
                }
            }
            if (p != null) {
                path.add(0, p);
            }
        }
        return path;
    }

    public static List<List<CAstSourcePositionMap.Position>> getPaths(Graph<Statement> g) {
        ArrayList<List<CAstSourcePositionMap.Position>> paths = new ArrayList<>();
        for (Statement src : getSources()) {
            for (Statement target : getTargets(src)) {
                // TODO: Override connected nodes of bfs pathfinder to only traverse those
                //   whose |sources| > 0.
                BFSPathFinder<Statement> finder = new BFSPathFinder<>(g, src, target);
                List<CAstSourcePositionMap.Position> path = getPositionsFromStatements(finder.find());
                if (!path.isEmpty()) {
                    paths.add(path);
                }
            }
        }
        return paths;
    }

}
