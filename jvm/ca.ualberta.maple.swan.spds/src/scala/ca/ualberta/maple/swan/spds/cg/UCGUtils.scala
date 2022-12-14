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

import ca.ualberta.maple.swan.ir.FunctionAttribute
import ca.ualberta.maple.swan.spds.structures.SWANControlFlowGraph.SWANBlock
import ca.ualberta.maple.swan.spds.structures.SWANMethod

import scala.collection.mutable.ArrayBuffer
import scala.collection.{immutable, mutable}

// The Depth First Worklist is a stack, and not a queue
// The Depth First Worklist will ignore blocks already in the worklist
final class DFWorklist(val options: CallGraphConstructor.Options) {
  private val hs: mutable.HashSet[SWANBlock] = new mutable.HashSet[SWANBlock]()
  private val stack: mutable.Stack[SWANBlock] = new mutable.Stack[SWANBlock]()

  def add(b: SWANBlock): Unit = {
    if (!hs.contains(b)) {
      hs.add(b)
      stack.push(b)
    }
  }

  def addMethod(m: SWANMethod): Unit = {
    // TODO move this to traverse method
    if (m.delegate.attribute.nonEmpty && m.delegate.attribute.get == FunctionAttribute.stub) {
      return
    }

    // TODO: Make this awful thing more efficient
    val toAdd = new ArrayBuffer[SWANBlock]()
    val worklist = new mutable.Queue[SWANBlock]()
    val added = new mutable.HashSet[SWANBlock]()
    worklist.enqueue(m.getStartBlock)
    added.add(m.getStartBlock)
    while (worklist.nonEmpty) {
      val b = worklist.dequeue()
      toAdd.append(b)
      m.getCFG.getBlockSuccsOf(b).foreach(s => {
        if (!added.contains(s)) {
          worklist.enqueue(s)
          added.add(s)
        }
      })
    }
    toAdd.reverse.foreach(add)
  }

  def isEmpty: Boolean = {
    stack.isEmpty
  }

  def nonEmpty: Boolean = {
    stack.nonEmpty
  }

  def pop(): SWANBlock = {
    val b = stack.pop()
    hs.remove(b)
    b
  }

  def next(): Option[SWANBlock] = {
    if (stack.isEmpty) {
      None
    }
    else {
      Some (pop())
    }
  }

}

final class DDGBitSet(val bitset: immutable.BitSet = immutable.BitSet.empty)(
  private implicit val ddgTypes: mutable.HashMap[String,Int],
  private implicit val ddgTypesInv: mutable.ArrayBuffer[String]) {

  def +(elem: String): DDGBitSet = {
    ddgTypes.get(elem) match {
      case Some(n) => new DDGBitSet(bitset + n)
      case None => this
    }
  }

  def union(that: DDGBitSet): DDGBitSet = {
    new DDGBitSet(bitset.concat(that.bitset))
  }

  def add(elem: String): DDGBitSet = {
    ddgTypes.get(elem) match {
      case Some(n) => new DDGBitSet(bitset + n)
      case None => this
    }
  }

  def contains(s: String): Boolean = {
    ddgTypes.get(s) match {
      case Some(n) => bitset.contains(n)
      case None => false
    }
  }

  def toHashSet: mutable.HashSet[String] = {
    mutable.HashSet.from(bitset.map(n => ddgTypesInv(n)))
  }

  def subsetOf(that: DDGBitSet): Boolean = {
    bitset.subsetOf(that.bitset)
  }

  def nonEmpty: Boolean = {
    bitset.nonEmpty
  }

}