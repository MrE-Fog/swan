//#SWAN#sources: "simpleTest.source() -> Swift.String"
//#SWAN#sinks: "simpleTest.sink(sunk: Swift.String) -> ()"

func source() -> String {
    return "I'm bad";
}

func sink(sunk: String) {
    print(sunk);
}

let sourced = source(); //source
sink(sunk: sourced); //sink