//**************************************************************************
// Debugger Target Composition API Sample Walkthrough
//
// Synthetic Symbol Builder
//**************************************************************************

//*************************************************
// INTRODUCTION
//*************************************************

The debugger's target composition API (DbgServices.h) allows plug-ins to come in and either teach
the debugger new functionality or alter behaviors of the debugger.  At present, this API is *MOSTLY*
focused on post-mortem targets (e.g.: dumps or similar) although certain functionality can be
useful in live targets as well.

The main notion of "target composition" is that a target that is being debugged is not a monolithic
construct.  It is, instead, a container of interdependent services, each of which can be implemented
by different plug-ins.  These services, in aggregate, provide the functionality which you observe in
the debugger.  As an example, when you issue a command in the debugger such as "db 1000", the
implementation of that command will go to the service container and find the appropriate service
to satisfy the request.  In the case of "db 1000", the debugger will ask for the virtual memory service
as identified by a GUID (DEBUG_SERVICE_VIRTUAL_MEMORY), get the memory access interface (ISvcMemoryAccess) 
from that service, and call a method (ReadMemory) on that interface.

Plug-ins for target composition are DbgEng style extensions that can be loaded and utilized in several different 
ways.  They can either be brought in to handle a particular post-mortem file format or they can be brought in
dynamically when certain conditions are met (e.g.: the underlying operating system is Windows or Linux,
the debugger is targeting a kernel mode or user mode target, a particular module is present in the module list,
etc...).

This sample is an example of a modification of the service container.  The sample plug-in inserts a symbol
provider into the service container.  This symbol provider allows the plug-in to handle symbols for any module
that it wishes instead of relying on PDBs or export symbols within a binary.

The sample also provides an upper-edge API through the debugger data model that allows dynamic manipulation of
types and symbol information via data model properties and methods.

//*************************************************
// BUILDING AND LOADING
//*************************************************

* Build the sample with the included Visual Studio solution

* Load the extension in the debugger (e.g.: ".load SymbolBuilderComposition.dll")

//*************************************************
// SOURCES TOUR
//*************************************************

This plug-in is separated into several distinct layers:

1) The core extension (boilerplate necessary to be an extension and register):

    * Extension.cpp             - The core exports required to be a debugger extension DLL
    
    * SymBuilder.h              - The main extension header

    * InternalGuids.h           - GUID definitions

Outside of the general boilerplate code required for any DLL to be a debugger extension, this plug-in is
separated into two very distinct layers:

2) The lower edge target composition layer (using DbgServices.h)

The primary portion of the extension is an implementation of a symbol provider and a set of symbols using the
target composition API.  It is *IMPERATIVE* that this part of the extension *NOT* touch anything at the data model
layer.  Many pieces of data model implementation are based upon target composition!  Files are:

    * SymbolServices.[h/cpp]    - The core service implementation of a symbol provider.  This is what handles
                                  requests to find symbols for any given module.

    * SymbolSet.[h/cpp]         - The base implementation of a symbol set.  Symbol sets are an abstraction of
                                  symbols for a particular module.  Each symbol set is implemented as a stacked
                                  set of interfaces.  All symbol sets must implement ISvcSymbolSet.  The more features
                                  the symbol set supports, the more ISvcSymbolSet* interfaces the symbol set
                                  implements.

    * SymbolBase.[h/cpp]        - A base class for all symbols that we support (e.g.: types, fields, global data,
                                  etc...)

    * SymbolTypes.[h/cpp]       - Classes necessary to implement types (e.g.: structs, unions, basic types, etc...)

    * SymbolData.[h/cpp]        - Classes necessary to implement data symbols (e.g.: fields, global variables, etc...)

    * SymManager.[h/cpp]        - A management service which is placed into the service container to keep track 
                                  of all of the synthetic symbols which have been created.  While this could have
                                  been placed within the symbol provider itself, it can often be easier for other
                                  services to access if it is within its own unique service (particularly if
                                  multiple symbol providers are aggregated together).

3) The upper edge data model layer (using DbgModel.h and DbgModelClientEx.h)

    ApiProvider.[h/cpp]         - The API exposed to the data model allowing for manipulation of symbols, types,
                                  and other functionality exposed by the lower edge target composition layer.

    HelpStrings.h               - The header for all resource strings for the data model APIs (e.g.: help text)

    HelpStrings.rc              - All of the resource strings for the data model APIs (e.g.: help text)

    ObjectModel.h               - The main header for the data model portions of the extension.  This is specifically
                                  separated from SymBuilder.h so that lower edge target composition portions of the 
                                  extension *CANNOT TOUCH* upper edge data model portions.

//*************************************************
// DATA MODEL API
//*************************************************

Everything provided by this debugger extension is accessed through a data model API.  There are two main
extensibility points of note:

The main point of access is a *CreateSymbols* API present on a namespace "SymbolBuilder" added to "Debugger.Utility":

    Debugger.Utility.SymbolBuilder
    ------------------------------
        CreateSymbols    [CreateSymbols(module) - Creates symbol builder symbols for the module in question.  'module' can be the name or base address of a module or a module object]

The CreateSymbols API will return an object representing the set of symbols which were just created.  Note that once 
symbol builder symbols have been created for a particular module, there will be a "SymbolBuilderSymbols" property
on the module object.  The value of "SymbolBuilderSymbols" is the same as the return value from the "CreateSymbols"
call.

    0:000> dx @$curprocess.Modules["ntdll.dll"]
    @$curprocess.Modules["ntdll.dll"]                 : ntdll.dll
        BaseAddress      : 0x7ffd29db0000
        Name             : ntdll.dll
        Size             : 0x1f5000
        Contents        
        SymbolBuilderSymbols

There are two properties on the symbol set object:

    Symbol Set Object
    -----------------
        Data             [The list of available global data]
        Types            [The list of available types]

The "Data" and "Types" properties, in addition to being lists, also have APIs to create new types or data:

    Types Object
    ------------
        AddBasicCTypes   [AddBasicCTypes() - For symbol builder symbols created without default C types, this adds the default C types to the type system]
        Create           [Create(typeName, [qualifiedTypeName]) - Creates a new user defined type.  An explicit 'qualifiedTypeName' may be optionally provided if different than the base name]
        CreateArray      [CreateArray(baseType, arraySize) - Creates a new array type.  'baseType' may either be a type object or a type name.  'arraySize' is the size of the array]
        CreateEnum       [CreateEnum(typeName, [basicType], [qualifiedTypeName]) - Creates a new enum type.  'basicType' is the type of the enum; if unspecified, the default is 'int'.  An explicit 'qualifiedTypeName' may optionally be provided if different than the base name]
        CreatePointer    [CreatePointer(baseType) - Creates a new pointer to the given type]
        CreateTypedef    [CreateTypedef(typeName, baseType, [qualifiedTypeName]) - Creates a new typedef to 'baseType'.  An explicit 'qualifiedTypeName' may optionally be provided if different than the base name]

    Data Object
    -----------
        CreateGlobal     [CreateGlobal(name, type, offset, [qualifiedName]) - Adds new global data of a specified 'type' at 'offset' bytes into the module.  An explicit 'qualifiedName' may optionally be provided if different than the base name]

UDTs (structs and classes) may be created through the "Create" call on the "Types" object:

    UDT Objects
    -----------
        Name              [The name of the symbol]
        QualifiedName     [The qualified name of the symbol]
        Size              [The size of the type as if sizeof(t) had been applied]
        Alignment         [The alignment of the type as if __alignof(t) had been applied]
        BaseClasses       [The list of base classes of the type]
        Fields            [The list of fields of the type]

        Delete            [Delete() - Deletes the type from the symbol builder symbols.  Any live objects of the given type are orphaned]

As with "Types" and "Data", the "Fields" and "BaseClasses" properties of a UDT object have methods available to create
fields and base classes in addition to providing lists:

    Fields Object
    -------------
        Add               [Add(name, type, [offset]) - Adds a new field of the given name and type.  'type' may be a type name or type object.  If 'offset' is supplied, the field will be placed at the given offset; otherwise, it will be placed automatically as if defined in a C structure]

    Base Classes Object
    -------------------
        Add               [Add(baseClassType, [offset]) - Adds a new base class of the given type.  If 'offset' is supplied, the base class will be placed at the given offset; otherwise, it will be placed automatically as if defined in a C++ struct/class]

Each added field has its own set of properties:

    Field Objects
    -------------
        Name              [The name of the symbol]
        QualifiedName     [The qualified name of the symbol]
        Parent            [The parent of the symbol]
        IsAutomaticLayout [Whether or not the field or enumerant has automatically assigned offsets and/or values]
        Type              [The type of the field]
        Offset            [The offset of the field]

        Delete            [Delete() - Deletes the field or enumerant from its parent type/enum]
        MoveBefore        [MoveBefore(pos) - Moves this field/enumerant to a location in its parent type before the given field/enumerant.  Note that 'pos' may either be a field/enumerant object or it may be a zero based ordinal indicating its position within the list of fields/enumerants]

    Base Class Objects
    ------------------
        Parent            [The parent of the symbol]
        IsAutomaticLayout [Indicates whether the base class has an automatically assigned offset within its parent type]
        Type              [The type of the base class]
        Offset            [The offset of the base class within its parent type]

        Delete            [Delete() - Deletes the base class from its parent type]
        MoveBefore        [MoveBefore(pos) - Moves this base class to a location in its parent type before the given base class.  Note that 'pos' may either be a base class object or it may be a zero based ordinal indicating its position within the list of base classes]

Enum types are similar to classes:

    Enum Type Objects
    -----------------
        Name              [The name of the symbol]
        QualifiedName     [The qualified name of the symbol]
        Size              [The size of the type as if sizeof(t) had been applied]
        Alignment         [The alignment of the type as if __alignof(t) had been applied]
        BaseType          [The basic underlying type of the enum]
        Enumerants        [The list of enumerants of the enum]

        Delete            [Delete() - Deletes the type from the symbol builder symbols.  Any live objects of the given type are orphaned]

The values (enumerants) within an enum are given by the "Enumerants" property.  Similar to various other lists
in this API, there are also some methods available to create new enumerants:

    Enumerants Object
    -----------------
        Add              [Add(name, [value]) - Adds a new enumerant of the given name and value.  If 'value' is supplied, the enumerant has an explicitly assigned value; otherwise, it will be assigned automatically as if defined in a C enum]

Each enumerant has properties similar to a field:

    Enumerant Objects
    -----------------
        Name              [The name of the symbol]
        QualifiedName     [The qualified name of the symbol]
        Parent            [The parent of the symbol]
        IsAutomaticLayout [Whether or not the field or enumerant has automatically assigned offsets and/or values]

        Delete            [Delete() - Deletes the field or enumerant from its parent type/enum]
        MoveBefore        [MoveBefore(pos) - Moves this field/enumerant to a location in its parent type before the given field/enumerant.  Note that 'pos' may either be a field/enumerant object or it may be a zero based ordinal indicating its position within the list of fields/enumerants]

Other types have a few similar properties:

    Array Type Objects
    ------------------
        Name              [The name of the symbol]
        QualifiedName     [The qualified name of the symbol]
        Size              [The size of the type as if sizeof(t) had been applied]
        Alignment         [The alignment of the type as if __alignof(t) had been applied]
        ArraySize         [The size of the array]
        BaseType          [The type which the array is an array of]

        Delete            [Delete() - Deletes the type from the symbol builder symbols.  Any live objects of the given type are orphaned]

    Pointer Type Objects
    --------------------
        Name              [The name of the symbol]
        QualifiedName     [The qualified name of the symbol]
        Size              [The size of the type as if sizeof(t) had been applied]
        Alignment         [The alignment of the type as if __alignof(t) had been applied]
        BaseType          [The type which the pointer points to]

        Delete            [Delete() - Deletes the type from the symbol builder symbols.  Any live objects of the given type are orphaned]

    Typedef Type Objects
    --------------------
        Name              [The name of the symbol]
        QualifiedName     [The qualified name of the symbol]
        Size              [The size of the type as if sizeof(t) had been applied]
        Alignment         [The alignment of the type as if __alignof(t) had been applied]
        BaseType          [The type which this typedef refers to]

        Delete           [Delete() - Deletes the type from the symbol builder symbols.  Any live objects of the given type are orphaned]

Global data which is created with the "CreateGlobal" API has a set of available properies as well:

    Global Data Objects
    -------------------
        Name              [The name of the symbol]
        QualifiedName     [The qualified name of the symbol]
        Type              [The type of the global data]
        Offset            [The offset of the global data within its loaded module]

        Delete            [Delete() - Deletes the global data]

//*************************************************
// STARTING OUT: A QUICK WALKTHROUGH
//*************************************************

When the debugger wants to find symbols for some module, it will go into the target composition service container
and ask the symbol provider if it can find symbols via a call to the "LocateSymbolsForImage" method.  This is the
main entry point for a plug-in which wants to handle some symbol format.  In this plug-in, you'll find that
code in "SymbolServices.[h/cpp]"

Typically, a symbol provider is registered as an *AGGREGATE* (so there can be multiple providers in one container).
Each symbol provider in turn gets LocateSymbolsForImage called.  If it wants to handle symbols, it creates
a symbol set which implements ISvcSymbolSet (at minimum) and returns success.  If it does not want to handle
symbols, it returns E_UNHANDLED_REQUEST_TYPE and the request is passed to the next symbol provider in line.  The
"symbol set" implementation for this plug-in can be found in "SymbolSet.[h/cpp]"

The symbol set in this plug-in manages several things:

    - A list of all symbols indexed by a unique ID.  Every reference from one symbol to another is done through 
      this unique ID and not a direct pointer.   This is the "m_symbols" list in the SymbolSet class.

    - A list of global symbols by unique ID.  This is a subset of the overall list containing symbols which are
      considered to be in the global scope of the symbol set.  This is the "m_globalSymbols" list in the 
      SymbolSet class.

    - An index of global names to unique IDs.  For all named global symbols, this is a mapping from qualified
      name to unique ID.  Symbols lookup by name is done through this index.  This is the "m_symbolNameMap"
      list in the SymbolSEt class.

    - A mapping of address ranges to symbols by unique ID.  All symbols (currently data) which have an address within
      the module are in this mapping.  This is the "m_symbolRanges" map within the SymbolSet class.  This mapping
      is a sorted list of non-overlapping address ranges and the IDs of symbols within those address ranges.
      Lookups of symbols by offset are done using this map.

There are several relationships between symbols managed through a link by unique ID:

    - m_parentId: The unique ID of the parent symbol.  A field's parent is its containing type.  A base class's
                  parent is its containing type.

    - m_children: The list of child symbols by unique ID.  Fields are children of a type.  Base classes are
                  children of a type.  

                  This is managed through:

                    AddChild:         Adds a symbol to the child list
                    RemoveChild:      Removes a symbol from the child list
                    GetChildPosition: Gets the position of a child in the list 
                    MoveChildBefore:  Moves the position of a given child within the list of children

    - m_dependentNotifySymbols: A list of symbols that must be notified if something about this symbol changes.
                                For a field, "class X { MyType fld; }", the symbol for "fld" has "X" in this
                                list.  This is not, however, solely a parent/child relationship.  The symbol
                                for "MyType" has "fld" in this list.  If some new field happens to be dynamically
                                added to "MyType", it will notify every symbol in its list.  This will cause
                                "fld" to be notified of the change...  which will, in turn, cause "X" to be
                                notified of the change...  which will, in turn, allow "X" to recompute its type
                                layout to deal with the reality of the new type system.

                                This is managed through:

                                    AddDependentNotify:    Adds a new symbol to the notification list
                                    RemoveDependentNotify: Removes a symbol from the notification list
                                    NotifyDependentChange: Called for every symbol in the list when something changes

The actual lookup of a symbol from its unique ID is done by an internal accessor "InternalGetSymbol" within the
symbol set.  As it is possible to delete symbols, it is entirely possible that the resolution of some symbol
by its unique ID may fail.  This may result in "zombie" symbols -- symbols which still exist but cannot be used
because something they refer to has been deleted.  Such symbols will cause failures of debugger commands but
should not cause undefined or crashing behaviors.

The general hierarchy of symbols (lower edge) presently in the plug-in:
**************************************************************************

BaseSymbol (the base class of any symbol)
    (SymbolBase.[h/cpp]
    ^
    |
    |----- BaseTypeSymbol (the base class of any type)
    |           (SymbolTypes.[h/cpp]
    |           ^
    |           |
    |           |----- BasicTypeSymbol   (the implementation of intrinsic types like "int")
    |           |
    |           |----- UdtTypeSymbol     (the implementation of any user defined type)
    |           |
    |           |----- PointerTypeSymbol (the implementation of any pointer type)
    |           |
    |           |----- ArrayTypeSymbol   (the implementation of any array type)
    |           |
    |           |----- TypedefTypeSymbol (the implementation of any typedef type)
    |           |
    |           |----- EnumTypeSymbol    (the implementation of any enum type)
    |
    |
    |----- BaseDataSymbol (the base class of any "data")
                (SymbolData.[h/cpp]
                ^
                |
                |----- UdtPositionalSymbol (the base class for things positioned within a UDT)
                |               (SymbolTypes.[h/cpp]
                |               ^
                |               |
                |               |----- FieldSymbol     (the implementation of a field within a UDT or an enumerant)
                |               |
                |               |----- BaseClassSymbol (the implementation of a base class within a UDT)
                |
                |
                |----- GlobalDataSymbol (the implementation of a global data symbol / global variable)

This hierarchy is *SOMEWHAT* mirrored on the API side (upper edge) within ApiProvider.[h/cpp]:
**************************************************************************

TypedInstanceModel<ComPtr<TSym>> (the factory base for any symbol from DbgModelClientEx.h)
    ^
    |      SymbolObjectHelpers   (helper classes for symbols)
    |             ^
    |             |
    |----- BaseSymbolObject<TSym> (the base class of the data model projection of any symbol)
    |             ^
    |             |         (TSym = TType)
    |             |----- BaseTypeObject<TType> (the base calss of the data model projection of any type)
    |             |              ^
    |             |              |         (TType = UdtTypeSymbol)
    |             |              |----- UdtTypeObject     (the projection for UDT types)
    |             |              |
    |             |              |         (TType = BasicTypeSymbol)
    |             |              |----- BasicTypeObject   (the projection for pointer types)
    |             |              |
    |             |              |         (TType = ArrayTypeSymbol)
    |             |              |----- PointerTypeObject (the projection for array types)
    |             |              |
    |             |              |         (TType = TypedefTypeSymbol)
    |             |              |----- TypedefTypeObject (the projection for typedef types)
    |             |              |
    |             |              |         (TType = EnumTypeSymbol)
    |             |              |----- EnumTypeObject    (the projection for enum types)
    |             |
    |             |
    |             |         (TSym = FieldSymbol)
    |             |----- FieldObject      (the projection for a field within a UDT)
    |             |
    |             |         (TSym = BaseClassSymbol)
    |             |----- BaseClassObject  (the projection for a base class of a UDT)
    |             |
    |             |         (TSym = GlobalDataSymbol)
    |             |----- GlobalDataObject (the projection for global data within a symbol set)
    | 
    |
    |   SymbolObjectHelpers       (helper class for symbols)
    |     ^         
    |     |        (TSym = UdtTypeSymbol)   
    |---------- FieldsObject      (the projection for the list of fields within a UDT)
    |     |   
    |     |        (TSym = EnumTypeSymbol)
    |---------- EnumerantsObject  (the projection for the list of enumerants in an enum)
    |     |
    |     |        (TSym = UdtTypeSymbol)
    |---------- BaseClassesObject (the projection for the list of base classes within a UDT)
    |     |
    |     |         (TSym = SymbolSet)
    |---------- DataObject        (the projection for the list of data in a symbol set)
    |     |
    |     |         (TSym = SymbolSet)
    |---------- TypesObject       (the projection for the list of types in a symbol set)

//*************************************************
// STARTING OUT: AN EXAMPLE
//*************************************************

Once the extension is loaded, symbols can be created with the "SymbolBuilderSymbols.CreateSymbols" API.  Take the
following example of windbg debugging notepad:

//
// Note that when I start the debugger, I have regular PDB symbols for ntdll:
//
0:000> lmmntdll
Browse full module list
start             end                 module name
00007ffd`29db0000 00007ffd`29fa5000   ntdll      (private pdb symbols)  C:\ProgramData\Dbg\sym\ntdll.pdb\094B224BC5297445CF29F9C9BB588DC91\ntdll.pdb

//
// Now, I load the debugger extension and ask it to create Symbol Builder symbols for ntdll.  It is important
// to note that these are not actually picked up until the debugger tries to load symbols for the given module.
// Since ntdll already had a PDB loaded, this requires an explicit .reload:
//
0:000> .load d:\xtn\SymbolBuilderComposition.dll
0:000> dx @$sym = Debugger.Utility.SymbolBuilder.CreateSymbols("ntdll.dll")
@$sym = Debugger.Utility.SymbolBuilder.CreateSymbols("ntdll.dll")                
    Data            
    Types           
0:000> .reload
Reloading current modules
...............
0:000> lmmntdll
Browse full module list
start             end                 module name
00007ffd`29db0000 00007ffd`29fa5000   ntdll      (service symbols: Symbol Builder Symbols)    

//
// With symbol builder symbols loaded, I can go and create a new type.  For instance, I'll create a type named
// "foo" with two int fields, "x" and "y":
//
0:000> dx @$foo = @$sym.Types.Create("foo")
@$foo = @$sym.Types.Create("foo")                 : UDT: foo ( size = 0, align = 1 )
    Name             : foo
    QualifiedName    : foo
    Size             : 0x0
    Alignment        : 0x1
    BaseClasses     
    Fields          
0:000> dx @$foo.Fields.Add("x", "int")
@$foo.Fields.Add("x", "int")                 : Field: x ( type = 'int', offset = 0 )
    Name             : x
    QualifiedName    : x
    Parent           : UDT: foo ( size = 4, align = 4 )
    IsAutomaticLayout : true
    Type             : Basic Type: int ( size = 4, align = 4 )
    Offset           : 0x0
0:000> dx @$foo.Fields.Add("y", "int")
@$foo.Fields.Add("y", "int")                 : Field: y ( type = 'int', offset = 4 )
    Name             : y
    QualifiedName    : y
    Parent           : UDT: foo ( size = 8, align = 4 )
    IsAutomaticLayout : true
    Type             : Basic Type: int ( size = 4, align = 4 )
    Offset           : 0x4

//
// Note that the resulting type can be used in any regular command in the debugger:
//
0:000> dt nt!foo
ntdll!foo
   +0x000 x                : Int4B
   +0x004 y                : Int4B
0:000> dx ((nt!foo *)@rip)
((nt!foo *)@rip)                 : 0x7ffd29e806b0 [Type: foo *]
    [+0x000] x                : 1208019916 [Type: int]
    [+0x004] y                : -1019689853 [Type: int]

//
// I can even create global data with the type in question:
//
0:000> dx @$sym.Data.CreateGlobal("test", "foo", 0x100)
@$sym.Data.CreateGlobal("test", "foo", 0x100)                 : Global Data: test ( type = 'foo', module offset = 256 )
    Name             : test
    QualifiedName    : test
    Type             : UDT: foo ( size = 8, align = 4 )
    Offset           : 0x100
0:000> dx nt!test
nt!test                 [Type: foo]
    [+0x000] x                : 336462347 [Type: int]
    [+0x004] y                : 1153024 [Type: int]
0:000> ln ntdll+100
Browse module
Set bu breakpoint

(00007ffd`29db0100)   ntdll!test   
Exact matches:
    ntdll!test = foo
0:000> ln ntdll+104
Browse module
Set bu breakpoint

(00007ffd`29db0100)   ntdll!test+0x4   
0:000> ln ntdll+109
Browse module
Set bu breakpoint

//
// I can manipulate the type system dynamically and changes will immediately show up in the debugger.  Here,
// I go and add a new field to the type "foo" and examine the changes:
//
0:000> dx @$foo.Fields.Add("z", "double")
@$foo.Fields.Add("z", "double")                 : Field: z ( type = 'double', offset = 8 )
    Name             : z
    QualifiedName    : z
    Parent           : UDT: foo ( size = 16, align = 8 )
    IsAutomaticLayout : true
    Type             : Basic Type: double ( size = 8, align = 8 )
    Offset           : 0x8
0:000> dt nt!foo
ntdll!foo
   +0x000 x                : Int4B
   +0x004 y                : Int4B
   +0x008 z                : Float
0:000> ln ntdll+109
Browse module
Set bu breakpoint

(00007ffd`29db0100)   ntdll!test+0x9   

********** READ THIS **********

A word of caution: the symbol builder will *CURRENTLY* allow the creation of constructs which are logically
impossible (e.g.: having class X have a base class of Y...  and class Y having a base class of X...  or similar
cyclic situations with fields).  The creation of such may lead the debugger into a bad state (including things like
stack overflows).

//*************************************************
// FUTURE ENHANCEMENTS
//*************************************************

We expect to grow this sample over time to explore more of the symbol API surface and make it more generally
useful.  Some of the future planned enhancements to this sample include:

1) Improved validation of the type system created (e.g.: no cyclic base classes or fields)

2) Hooking up the synthetic types JavaScript extension and its "C header parser" up to this extension.

3) The ability to "import" data from other symbol sources (or stack on top of them) so that the sample is not
   an "either-or" choice of using the symbol builder or using the generally available (whether public or private) 
   symbols for a given module.  This would allow the symbol builder to be used to simply "extend" the symbols already
   available for a module rather than replacing them.

4) Support for functions, arguments, and scopes so that there are APIs to add function symbols with a prototype,
   arguments, variables, and lifetime information.

5) Support for using the data model disassembler to walk through a function tracing the position of function arguments
   from a prototype and calling convention (e.g.: parameters in rcx, rdx, r8, and r9 on x64) so that you can get 
   consistent examination of parameters to public APIs on public symbols.

6) Support for serializing and deserializing the symbol builder information for a given module.

