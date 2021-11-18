/*
 * Copyright (c) 2021 the SWAN project authors. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This software has dependencies with other licenses.
 * See https://github.com/themaplelab/swan/doc/LICENSE.md.
 */

package ca.ualberta.maple.swan.spds.cg

import ca.ualberta.maple.swan.ir.ModuleGroup
import ca.ualberta.maple.swan.utils.Logging

abstract class CallGraphConstructor(val moduleGroup: ModuleGroup) {

  def buildCallGraph(): CallGraphUtils.CallGraphData = {
    Logging.printInfo("Constructing Call Graph")
    val startTimeNano = System.nanoTime()
    val startTimeMs = System.currentTimeMillis()
    // This initialization includes converting SWIRL to SPDS form
    val cg = CallGraphUtils.initializeCallGraph(moduleGroup)
    cg.msTimeToInitializeCG = System.currentTimeMillis() - startTimeMs
    val startTimeActualMs = System.currentTimeMillis()
    buildSpecificCallGraph(cg)
    CallGraphUtils.pruneEntryPoints(cg)
    cg.msTimeToConstructCG = System.currentTimeMillis() - startTimeMs
    cg.msTimeActualCGConstruction = System.currentTimeMillis() - startTimeActualMs
    var overHeadTime = cg.msTimeToConstructCG
    cg.specificData.foreach(s => overHeadTime -= s.time)
    cg.msTimeOverhead = overHeadTime
    Logging.printTimeStampSimple(0, startTimeNano, "constructing")
    System.out.println(cg)
    cg
  }

  def buildSpecificCallGraph(cg: CallGraphUtils.CallGraphData): Unit
}