# ETIL Epic SnowBoundTwo

- Two `Arenas` define the scope of work: `Tasks` and `Features`
- Address the Tasks first as separate Claude Plans, then the Features as separate Claude Plans.
- Ideally each Claude Plan shall be evaluated, designed, implemented, and committed before beginning the next Plan.

## Arena: Tasks

- **Task: Move word definitions completely out of the interpreter**
    - The current `Interpreter::interpret_token` and `Interpreter::compile_control_flow` methods have ladders of if
      statements like:
  ```c++
  if (token == "<someword>")
  {
      // Word implementation
      return true;
  }
  ```
    - This hard coded linear word search is not scalable.
    - If you're going to look up words use something scalable like a HashMap<string, Function> but thread safe.


- **Task: Replace hard coded words with their ETIL functional equivalent written in ETIL.**
    - There are words in `Interpreter::interpret_token` and `Interpreter::compile_control_flow`
      that can be implemented in ETIL using the existing ETIL primitives, i.e., ETIL written in ETIL.
    - The metadata words seem like good candidites for conversion.
    - The control flow words can definitely be implemented using `create` and `does>`
    - The converted words need to stored as ETIL source .til files and loaded on startup.
    - The `.etil/interpreter` directory should contain a JSON configuration file that
      specifies the .til files to be loaded on startup and their order of loading.

## Arena: Features

### Feature: Enhanced Built In Help via Metadata

**Goal:** To use the metadata functionality to provide general help documentation and
  information including exhaustive functional descriptions, stack parameter information,
  and any other information pertinent to coding with a given word.
- Create a ETIL word that iterates through all the words, generating the metadata docs, 
  writing the docs to a timestamped .til file.
- Save the file in the configuration file directory.
- Add the .til file to the configuration file that cause it to load the metadata .til 
file on startup.

### Feature: Lambda Functions
**Goal**: Provide the infrastructure to support the Asynchronous Processing and I/O features.
- Closures?
- Callbacks & Event Handlers

### Feature: Asynchronous Processing
** Goal**: To provide generic structure to Asynchronous functionality. 
- Singular Async: Promises/Futures/await
- Streaming Async: Observables .aka RxJS Observables

### Feature: File and Network I/O
**Goal**: To provide basic File and Network I/O functionality. 
- I favor the Node.js dual I/O API strategy where there are parings of synchronous and asynchronous functions (words)
  that provide the functionality.
- Asynchronous I/O should favor the use of Observables.

### Feature: MongoDB Persistence .aka. Libraries
**Goal**: To provide secure persistent storage between ETIL sessions.
- Use MongoDB as a JSON based unlimited 'block store' called `Libraries`;
- C++ Asynchronous I/O to/from the MongoDB server, Observables stream the result.
- TIL code and metadata definition storage.
  - Load/Save *versioned* TIL code.
- Standard MongoDB Collections used as backing store for a DataMap feature:
    - DataMap works like a large scale associative array with asynchronous get/put operations.
    - Each individual DataMap is backed by a single to a MongoDB collection.
    - DataMaps use MongoDB JSON query syntax.
    - Asynchronous with Observable results.
- Optional authenticated MongoDB connections, with Roles, etc. 