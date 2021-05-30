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

package ca.ualberta.maple.swan.drivers

import java.io.{File, FileWriter}
import java.nio.charset.StandardCharsets
import java.nio.file.{Files, Paths}

import ca.ualberta.maple.swan.ir._
import ca.ualberta.maple.swan.ir.canonical.SWIRLPass
import ca.ualberta.maple.swan.ir.raw.SWIRLGen
import ca.ualberta.maple.swan.parser.{SILModule, SILParser}
import ca.ualberta.maple.swan.spds.analysis.{AnalysisType, TaintAnalysis, TaintAnalysisOptions}
import ca.ualberta.maple.swan.utils.Logging
import org.apache.commons.io.{FileExistsException, FileUtils, IOUtils}
import picocli.CommandLine
import picocli.CommandLine.{Command, Option, Parameters}

import scala.collection.mutable.ArrayBuffer

object Driver {
  /* Because this driver can be invoked programmatically, most picocli options
   * (@Option) should have a matching field in Driver.Options.
   */
  class Options {
    var debug = false
    var single = false
    var cache = false
    var dumpFunctionNames = false
    var spec: scala.Option[File] = None
    var pathTracking = false
    var silModuleCB: SILModule => Unit = _
    var rawSwirlModuleCB: Module => Unit = _
    var canSwirlModuleCB: CanModule => Unit = _
    def debug(v: Boolean): Options = {
      this.debug = v; this
    }
    def cache(v: Boolean): Options = {
      this.cache = v; this
    }
    def single(v: Boolean): Options = {
      this.single = v; this
    }
    def dumpFunctionNames(v: Boolean): Options = {
      this.dumpFunctionNames = v; this
    }
    def spec(v: File): Options = {
      this.spec = scala.Option(v); this
    }
    def pathTracking(v: Boolean): Options = {
      this.pathTracking = v; this
    }
    def addSILCallBack(cb: SILModule => Unit): Options = {
      silModuleCB = cb; this
    }
    def addRawSWIRLCallBack(cb: Module => Unit): Options = {
      rawSwirlModuleCB = cb; this
    }
    def addCanSWIRLCallBack(cb: CanModule => Unit): Options = {
      canSwirlModuleCB = cb; this
    }
  }

  def main(args: Array[String]): Unit = {
    val exitCode = new CommandLine(new Driver).execute(args:_*)
    System.exit(exitCode);
  }
}

/**
 * This is the main driver for SWAN. The driver can either be invoked from the
 * command line or programmatically (e.g., with a test driver) using runActual().
 */
@Command(name = "SWAN Driver", mixinStandardHelpOptions = true, header = Array(
  "@|fg(208)" +
  "    WNW                                                                   WWW \n" +
  "  WOdkXW                                                              WNKOOOXW\n" +
  "  Xd:clx0XWW                                                      WWNK0kxxxx0WW\n"+
  " WOl::cccoxOXNW                                               WNXK0OkxxdddddONW\n"+
  " WOc:::cccccldk0KNNWW                 WWWWk             WNNXK0Okxdddddxddddx0NW\n"+
  " W0l:::::ccccccclodxkOO00KNW        WKkxxOXW           WXOxddooodddddddddxxkXW\n" +
  "  Xd::::::ccccccccccccclllx0N      WOlcccl0         WXkoooooooooodddddddxOXW \n"  +
  "  WKo::::::ccccccccccccclllokXW    Nxo   xK       NKkolllllooooooooddxO0XW   \n"  +
  "   WXxl::::::cccccccccccccllldONW   xkN        WN0dlccclllllooooooooxKW      \n"  +
  "     WX0dc:::::cccccccccccccllldOXW  xOXN    WXkocccccccclllllloooookN       \n"  +
  "       WKo::::::cccccccccccccllllok0  kxxxxk   WcccccccccllllllooxOXW        \n"  +
  "        WOo::::::ccccccccccclccllllod   XKOdlk   WkcccccccloddkOKNW          \n"  +
  "         WXOdlc:::cccccccccccccclldxOKN   WNklck  Wkccccd0XNWW               \n"  +
  "            WX0OkkOkdlcccccccccldOXW       W0lclk  WccloxOXW                 \n"  +
  "                   WN0xdolllloodOX        dWXKKXN                            \n"  +
  "                      WWNXKKKXNWW  WX0OkxxddxkKN                             \n"  +
  "                                      WNNNNWW                                 |@" +
  "\n\n Copyright (c) 2021 the SWAN project authors. All rights reserved.\n"         +
  " Licensed under the Apache License, Version 2.0, available at\n"                  +
  " http://www.apache.org/licenses/LICENSE-2.0\n"                                    +
  " This software has dependencies with other licenses.\n"                           +
  " See https://github.com/themaplelab/swan/doc/LICENSE.md.\n"))
class Driver extends Runnable {

  @Option(names = Array("-s", "--single"),
    description = Array("Use a single thread."))
  private val singleThreaded = new Array[Boolean](0)

  @Option(names = Array("-d", "--debug"),
    description = Array("Dump IRs and changed partial files to debug directory."))
  private val debugPrinting = new Array[Boolean](0)

  @Option(names = Array("-n", "--names"),
    description = Array("Dump functions names to file in debug directory (e.g., for finding sources/sinks)."))
  private val dumpFunctionNames = new Array[Boolean](0)

  @Option(names = Array("-j", "--json-spec"),
    description = Array("JSON specification file for analysis (necessary for analysis)."))
  private val spec: File = null

  @Option(names = Array("-p", "--path-tracking"),
    description = Array("Enable path tracking for taint analysis (experimental and is known to hang)."))
  private val pathTracking = new Array[Boolean](0)

  @Option(names = Array("-i", "--invalidate-cache"),
    description = Array("Invalidate cache."))
  private val invalidateCache = new Array[Boolean](0)

  @Option(names = Array("-f", "--force-cache-read"),
    description = Array("Force reading the cache, regardless of changed files."))
  private val forceRead = new Array[Boolean](0)

  @Option(names = Array("-c", "--cache"),
    description = Array("Cache SIL and SWIRL group module. This is experimental, slow, and incomplete (DDGs and CFGs not serialized)."))
  private val useCache = new Array[Boolean](0)

  @Parameters(arity = "1", paramLabel = "swan-dir", description = Array("swan-dir to process."))
  private val inputFile: File = null

  var options: Driver.Options = _

  /**
   * Convert picocli options to Driver.Options and call runActual.
   */
  override def run(): Unit = {
    options = new Driver.Options()
      .debug(debugPrinting.nonEmpty)
      .cache(useCache.nonEmpty)
      .single(singleThreaded.nonEmpty)
      .dumpFunctionNames(dumpFunctionNames.nonEmpty)
      .spec(spec)
      .pathTracking(pathTracking.nonEmpty)
    runActual(options, inputFile)
  }

  /**
   * Processes the given swanDir (translation and analysis) and returns
   * the module group. Can return null if the given directory is empty or
   * if a cache exists and there is no change.
   */
  def runActual(options: Driver.Options, swanDir: File): ModuleGroup = {
    if (!swanDir.exists()) {
      throw new FileExistsException("swan-dir does not exist")
    }
    val runStartTime = System.nanoTime()
    val proc = new SwanDirProcessor(swanDir, options, invalidateCache.nonEmpty, forceRead.nonEmpty)
    val treatRegular = !options.cache || invalidateCache.nonEmpty || !proc.hadExistingCache
    if (!treatRegular) Logging.printInfo(proc.toString)
    // Check early exit conditions
    if (proc.files.isEmpty || (options.cache && !proc.changeDetected)) return null
    val debugDir: File = {
      if (options.debug) {
        val dd = Files.createDirectories(
          Paths.get(swanDir.getPath, "debug-dir")).toFile
        FileUtils.cleanDirectory(dd)
        dd
      } else {
        null
      }
    }
    val threads = new ArrayBuffer[Thread]()
    val silModules = new ArrayBuffer[SILModule]()
    val rawModules = new ArrayBuffer[Module]()
    val canModules = new ArrayBuffer[CanModule]()
    // Large files go first so we can immediately thread them
    proc.files.sortWith(_.length() > _.length()).foreach(f => {
      def go(): Unit = {
        val res = runner(debugDir, f, options)
        silModules.append(res._1)
        rawModules.append(res._2)
        canModules.append(res._3)
      }
      // Don't bother threading for <10MB
      if (options.single || f.length() < 10485760) {
        go()
      } else {
        val t = new Thread {
          override def run(): Unit = {
            go()
          }
        }
        threads.append(t)
        t.start()
      }
    })
    if (treatRegular) {
      // Single file for now, iterating files is tricky with JAR resources)
      val in = this.getClass.getClassLoader.getResourceAsStream("models.swirl")
      val modelsContent = IOUtils.toString(in, StandardCharsets.UTF_8)
      val res = modelRunner(debugDir, modelsContent, options)
      rawModules.append(res._1)
      canModules.append(res._2)
    }
    threads.foreach(t => t.join())
    val group = {
      if (treatRegular) {
        ModuleGrouper.group(canModules)
      } else {
        ModuleGrouper.group(canModules, proc.cachedGroup, proc.changedFiles)
      }
    }
    Logging.printTimeStampSimple(0, runStartTime, "(group ready for analysis)")
    Logging.printInfo(
      group.toString+group.functions.length+" functions\n"+
      group.entries.size+" entries")
    if (options.debug) writeFile(group, debugDir, "grouped")
    if (options.cache) proc.writeCache(group)
    if (options.dumpFunctionNames) {
      val f = Paths.get(swanDir.getPath, "function-names.txt").toFile
      val fw = new FileWriter(f)
      group.functions.foreach(f => {
        fw.write(f.name + "\n")
      })
      fw.close()
    }
    if (options.spec.nonEmpty) {
      val f = Paths.get(swanDir.getPath, "results.json").toFile
      val fw = new FileWriter(f)
      try {
        val specs = TaintAnalysis.Specification.parse(options.spec.get)
        val r = new ArrayBuffer[ujson.Obj]
        specs.foreach(spec => {
          val analysisOptions = new TaintAnalysisOptions(
            if (options.pathTracking) AnalysisType.ForwardPathTracking
            else AnalysisType.Forward)
          val analysis = new TaintAnalysis(group, spec, analysisOptions)
          val results = analysis.run()
          Logging.printInfo(results.toString)
          val json = ujson.Obj("name" -> spec.name)
          val paths = new ArrayBuffer[ujson.Value]
          results.paths.foreach(path => {
            val jsonPath = ujson.Obj("source" -> path.source)
            jsonPath("sink") = path.sink
            jsonPath("path") = path.nodes.filter(_._2.nonEmpty).map(_._2.get.toString)
            paths.append(jsonPath)
          })
          json("paths") = paths
          r.append(json)
        })
        val finalJson = ujson.Value(r)
        fw.write(finalJson.render(2))
      } finally {
        fw.close()
      }
    }
    group
  }

  /** Processes a SIL file. */
  def runner(debugDir: File, file: File, options: Driver.Options): (SILModule, Module, CanModule) = {
    val partial = file.getName.endsWith(".changed")
    val silParser = new SILParser(file.toPath)
    val silModule = silParser.parseModule()
    if (options.silModuleCB != null) options.silModuleCB(silModule)
    val rawSwirlModule = new SWIRLGen().translateSILModule(silModule)
    if (partial && rawSwirlModule.functions(0).name.startsWith("SWAN_FAKE_MAIN")) rawSwirlModule.functions.remove(0)
    if (options.rawSwirlModuleCB != null) options.rawSwirlModuleCB(rawSwirlModule)
    if (options.debug) writeFile(rawSwirlModule, debugDir, file.getName + ".raw")
    val canSwirlModule = new SWIRLPass().runPasses(rawSwirlModule)
    if (options.canSwirlModuleCB != null) options.canSwirlModuleCB(canSwirlModule)
    if (options.debug) writeFile(canSwirlModule, debugDir, file.getName)
    (silModule, rawSwirlModule, canSwirlModule)
  }

  /** Processes a SWIRL model file. */
  def modelRunner(debugDir: File, modelsContent: String, options: Driver.Options): (Module, CanModule) = {
    val swirlModule = new SWIRLParser(modelsContent, model = true).parseModule()
    if (options.debug) writeFile(swirlModule, debugDir, "models.raw")
    val canSwirlModule = new SWIRLPass().runPasses(swirlModule)
    if (options.debug) writeFile(canSwirlModule, debugDir, "models")
    (swirlModule, canSwirlModule)
  }

  /** Write a module to the debug directory. */
  def writeFile(module: Object, debugDir: File, prefix: String): Unit = {
    val printedSwirlModule = {
      module match {
        case canModule: CanModule =>
          new SWIRLPrinter().print(canModule, new SWIRLPrinterOptions)
        case rawModule: Module =>
          new SWIRLPrinter().print(rawModule, new SWIRLPrinterOptions)
        case groupModule: ModuleGroup =>
          new SWIRLPrinter().print(groupModule, new SWIRLPrinterOptions)
        case _ =>
          throw new RuntimeException("unexpected")
      }
    }
    val f = Paths.get(debugDir.getPath, prefix + ".swirl").toFile
    val fw = new FileWriter(f)
    Logging.printInfo("Writing " + module.toString + " to " + f.getName)
    fw.write(printedSwirlModule)
    fw.close()
  }
}