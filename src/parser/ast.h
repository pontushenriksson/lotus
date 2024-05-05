// Copyright 2024 Lucas Norman

#pragma once

#include <iostream>
#include <memory>
#include <vector>
#include <map>

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Verifier.h"

#include "../diagnostics/generator.h"

// to keep track of the variables that are available in the current scope/function when generating LLVM IR
static std::map<std::string, llvm::AllocaInst*> namedValues;

// helper function to get the llvm::Type* from a type name/string
static llvm::Type *getLLVMType(const std::string& type, const std::unique_ptr<llvm::IRBuilder<>>& builder) {
    if (type == "i32") {
        return builder->getInt32Ty();
    } else if (type == "i64") {
        return builder->getInt64Ty();
    } else if (type == "f32") {
        return builder->getFloatTy();
    } else if (type == "f64") {
        return builder->getDoubleTy();
    } else if (type == "char") {
        return builder->getInt8Ty();
    } else {
        // return a void type
        return builder->getVoidTy();
    }
}

// helper function for converting llvm value to boolean type
static llvm::Value *getBooleanValue(llvm::Value *value, const std::unique_ptr<llvm::IRBuilder<>>& builder) {
    if (value->getType()->isIntegerTy(32)) {
        // convert from i32 to boolean (i1)
        return builder->CreateICmpNE(value, builder->getInt32(0), "ifcond");
    } else if (value->getType()->isIntegerTy(1)) {
        // is already a boolean type (i1)
        return value;
    } else {
        // throw an error since the type cannot be converted to boolean
        std::string stringType;
        llvm::raw_string_ostream stream(stringType);
        value->getType()->print(stream);
        generator::fatal_error(std::chrono::high_resolution_clock::now(), "Cannot convert expression to boolean type", "The expression of type '" + stream.str() + "' could not be converted to boolean");
        return nullptr;
    }
}

// helper function to create an alloca instruction in the entry block of a function. Used with mutable variables
static llvm::AllocaInst *createEntryBlockAlloca(llvm::Function *fn, const std::string& variableName, llvm::Type *type) {
    llvm::IRBuilder<> temporaryBuilder(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return temporaryBuilder.CreateAlloca(type, nullptr, variableName);
}

enum CodegenResultType {
    VALUE_CODEGEN_RESULT,
    PARAM_CODEGEN_RESULT,
    FUNCTION_CODEGEN_RESULT
};

// type to store parameter result in CodegenResult, since it has two fields
struct ParamCodegenResult {
    std::string identifier;
    llvm::Type *type;

    ParamCodegenResult(std::string identifier, llvm::Type *type): identifier(identifier), type(type) {}
    ~ParamCodegenResult() {}
};

// type to return from codegen methods, to handle multiple return types like llvm::Value* and llvm::Function*
struct CodegenResult {
    union {
        llvm::Value *value;
        ParamCodegenResult param;
        llvm::Function *fn;
    };
    CodegenResultType resultType;

    CodegenResult(llvm::Value *value, CodegenResultType resultType): value(value), resultType(resultType) {}
    CodegenResult(ParamCodegenResult param, CodegenResultType resultType): param(param), resultType(resultType) {}
    CodegenResult(llvm::Function *fn, CodegenResultType resultType): fn(fn), resultType(resultType) {}

    ~CodegenResult() {}
};

class ASTNode {
 public:
    virtual ~ASTNode() {}

    virtual void print(int depth) const = 0;
    virtual std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const = 0;
};

class AST {
    std::vector<ASTNode*> rootNodes;

 public:
    explicit AST(std::vector<ASTNode*> rootNodes): rootNodes(rootNodes) {}
    ~AST() {
        // delete each node the vector
        for (ASTNode *node : rootNodes) {
            delete node;
        }
    }

    void print() const {
        // print each node the vector
        for (ASTNode *node : rootNodes) {
            node->print(0);
        }
    }

    void codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const {
        // codegen each node the vector
        for (ASTNode *node : rootNodes) {
            node->codegen(ctx, builder, moduleLLVM);
        }
    }
};

class ASTVariableExpression : public ASTNode {
    std::string identifier;

 public:
    explicit ASTVariableExpression(std::string identifier): identifier(identifier) {}
    ~ASTVariableExpression() {}  // no child nodes

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Variable Expression: " << identifier << '\n';
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        llvm::AllocaInst *allocaInstance = namedValues[identifier];
        if (allocaInstance == nullptr) {
            generator::fatal_error(std::chrono::high_resolution_clock::now(), "Unknown variable name", "The variable '" + identifier + "' could not be found");
            return nullptr;
        }
        return std::make_unique<CodegenResult>(builder->CreateLoad(allocaInstance->getAllocatedType(), allocaInstance, identifier.c_str()), VALUE_CODEGEN_RESULT);
    }
};

class ASTNumber : public ASTNode {
    int number;

 public:
    explicit ASTNumber(int number): number(number) {}
    ~ASTNumber() {}  // no child nodes

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Number: " << number << '\n';
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        return std::make_unique<CodegenResult>(builder->getInt32(number), VALUE_CODEGEN_RESULT);
    }
};

class ASTString : public ASTNode {
    std::string text;

 public:
    explicit ASTString(std::string text): text(text) {}
    ~ASTString() {}  // no child nodes

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "String: " << text << '\n';
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        return std::make_unique<CodegenResult>(builder->CreateGlobalStringPtr(text), VALUE_CODEGEN_RESULT);
    }
};

class ASTChar : public ASTNode {
    char character;

 public:
    explicit ASTChar(char character): character(character) {}
    ~ASTChar() {}  // no child nodes

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Character: " << character << '\n';
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        return std::make_unique<CodegenResult>(builder->getInt8(character), VALUE_CODEGEN_RESULT);
    }
};

class ASTCompoundStatement : public ASTNode {
    std::vector<ASTNode*> statementList;

 public:
    explicit ASTCompoundStatement(std::vector<ASTNode*> statementList): statementList(statementList) {}
    // also add an empty constructor for no statements
    ASTCompoundStatement() {}
    ~ASTCompoundStatement() {
        // delete each node the vector
        for (ASTNode *statement : statementList) {
            delete statement;
        }
    }

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Compound Statement:\n";
        // print each node the vector
        for (ASTNode *statement : statementList) {
            statement->print(depth + 1);
        }
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        // save the named values to be able to restore them later
        std::map<std::string, llvm::AllocaInst*> savedNamedValues = namedValues;

        // codegen each node in the vector
        for (ASTNode *statement : statementList) {
            statement->codegen(ctx, builder, moduleLLVM);
            // if the latest basic block has a terminal statement, then skip generating the rest
            if (builder->GetInsertBlock()->getTerminator()) return nullptr;
        }

        // restore the named values
        namedValues = savedNamedValues;
        return nullptr;
    }
};

class ASTBinaryOperator : public ASTNode {
    ASTNode *left;
    ASTNode *right;
    std::string operation;

 public:
    ASTBinaryOperator(ASTNode *left, ASTNode *right, std::string operation): left(left), right(right), operation(operation) {}
    ~ASTBinaryOperator() {
        delete left;
        delete right;
    }

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Binary Operator: " << operation << '\n';
        left->print(depth + 1);
        right->print(depth + 1);
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        std::unique_ptr<CodegenResult> leftResult = left->codegen(ctx, builder, moduleLLVM);
        std::unique_ptr<CodegenResult> rightResult = right->codegen(ctx, builder, moduleLLVM);
        // check if both the left and right results are valid
        if (leftResult == nullptr || leftResult->resultType != VALUE_CODEGEN_RESULT || rightResult == nullptr || rightResult->resultType != VALUE_CODEGEN_RESULT) return nullptr;
        llvm::Value *resultValue;
        if (operation == "+") {
            resultValue = builder->CreateAdd(leftResult->value, rightResult->value, "addtmp");
        } else if (operation == "-") {
            resultValue = builder->CreateSub(leftResult->value, rightResult->value, "subtmp");
        } else if (operation == "*") {
            resultValue = builder->CreateMul(leftResult->value, rightResult->value, "multmp");
        } else if (operation == "==") {
            resultValue = builder->CreateICmpEQ(leftResult->value, rightResult->value, "cmptmpequals");
        } else if (operation == "!=") {
            resultValue = builder->CreateICmpNE(leftResult->value, rightResult->value, "cmptmpnotequals");
        } else if (operation == "<") {
            resultValue = builder->CreateICmpSLT(leftResult->value, rightResult->value, "cmptmpless");
        } else if (operation == ">") {
            resultValue = builder->CreateICmpSGT(leftResult->value, rightResult->value, "cmptmpgreater");
        } else if (operation == "<=") {
            resultValue = builder->CreateICmpSLE(leftResult->value, rightResult->value, "cmptmplessequals");
        } else if (operation == ">=") {
            resultValue = builder->CreateICmpSGE(leftResult->value, rightResult->value, "cmptmpgreaterequals");
        } else if (operation == "&&") {
            resultValue = builder->CreateAnd(leftResult->value, rightResult->value, "andtmp");
        } else if (operation == "||") {
            resultValue = builder->CreateOr(leftResult->value, rightResult->value, "ortmp");
        } else {
            generator::fatal_error(std::chrono::high_resolution_clock::now(), "Invalid binary operator", "The operator '" + operation + "' is not supported");
            return nullptr;
        }
        return std::make_unique<CodegenResult>(resultValue, VALUE_CODEGEN_RESULT);
    }
};

class ASTUnaryOperator : public ASTNode {
    ASTNode *expression;
    std::string operation;

 public:
    ASTUnaryOperator(ASTNode *expression, std::string operation): expression(expression), operation(operation) {}
    ~ASTUnaryOperator() {
        delete expression;
    }

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Unary Operator: " << operation << '\n';
        expression->print(depth + 1);
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        std::unique_ptr<CodegenResult> expressionResult = expression->codegen(ctx, builder, moduleLLVM);
        // check if the expression result is valid
        if (expressionResult == nullptr || expressionResult->resultType != VALUE_CODEGEN_RESULT) return nullptr;
        llvm::Value *resultValue;
        if (operation == "!") {
            resultValue = builder->CreateNot(expressionResult->value, "nottmp");
        } else if (operation == "-") {
            resultValue = builder->CreateNeg(expressionResult->value, "negtmp");
        } else if (operation == "+") {
            // does not change the value
            resultValue = expressionResult->value;
        }
        return std::make_unique<CodegenResult>(resultValue, VALUE_CODEGEN_RESULT);
    }
};

class ASTParameter : public ASTNode {
    std::string identifier;
    std::string type;

 public:
    ASTParameter(std::string identifier, std::string type): identifier(identifier), type(type) {}
    ~ASTParameter() {}  // no child nodes

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Parameter:\n";
        std::cout << std::string((depth + 1) * 2, ' ') << "Identifier: " << identifier << "\n";
        std::cout << std::string((depth + 1) * 2, ' ') << "Type: " << type << "\n";
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        return std::make_unique<CodegenResult>(ParamCodegenResult(identifier, getLLVMType(type, builder)), PARAM_CODEGEN_RESULT);
    }
};

class ASTFunctionPrototype : public ASTNode {
    std::string identifier;
    std::vector<ASTNode*> parameterList;
    std::string returnType;

 public:
    ASTFunctionPrototype(std::string identifier, std::vector<ASTNode*> parameterList, std::string returnType): identifier(identifier), parameterList(parameterList), returnType(returnType) {}
    ~ASTFunctionPrototype() {
        // delete each node the vector
        for (ASTNode *parameter : parameterList) {
            delete parameter;
        }
    }

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Function Prototype:\n";
        std::cout << std::string((depth + 1) * 2, ' ') << "Identifier: " << identifier << "\n";
        if (!parameterList.empty()) {
            std::cout << std::string((depth + 1) * 2, ' ') << "Parameters:\n";
            // print each node the vector
            for (ASTNode *parameter : parameterList) {
                parameter->print((depth + 1) + 1);
            }
        } else {
            std::cout << std::string((depth + 1) * 2, ' ') << "No Parameters\n";
        }
        // check if string is not empty
        if (!returnType.empty())
            std::cout << std::string((depth + 1) * 2, ' ') << "Return type: " << returnType << "\n";
        else
            std::cout << std::string((depth + 1) * 2, ' ') << "No Return Type\n";
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        std::vector<std::string> paramNames;
        std::vector<llvm::Type*> paramTypes;

        // generate the parameters
        for (ASTNode *parameter : parameterList) {
            std::unique_ptr<CodegenResult> parameterResult = parameter->codegen(ctx, builder, moduleLLVM);
            if (parameterResult == nullptr || parameterResult->resultType != PARAM_CODEGEN_RESULT) return nullptr;
            paramNames.push_back(parameterResult->param.identifier);
            paramTypes.push_back(parameterResult->param.type);
        }

        // return type, parameters, varargs
        llvm::FunctionType *fnType = llvm::FunctionType::get(getLLVMType(returnType, builder), paramTypes, false);
        llvm::Function *fn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, identifier, *moduleLLVM);

        // set the parameter names
        int i = 0;
        for (auto &arg : fn->args()) {
            arg.setName(paramNames[i++]);
        }

        return std::make_unique<CodegenResult>(fn, FUNCTION_CODEGEN_RESULT);
    }
};

class ASTFunctionDefinition : public ASTNode {
    ASTNode *prototype;
    ASTNode *body;

 public:
    ASTFunctionDefinition(ASTNode *prototype, ASTNode *body): prototype(prototype), body(body) {}
    ~ASTFunctionDefinition() {
        delete prototype;
        delete body;
    }

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Function Definition:\n";
        prototype->print(depth + 1);
        body->print(depth + 1);
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        // generate the function prototype and create the entry block
        std::unique_ptr<CodegenResult> prototypeResult = prototype->codegen(ctx, builder, moduleLLVM);
        if (prototypeResult == nullptr || prototypeResult->resultType != FUNCTION_CODEGEN_RESULT) return nullptr;
        llvm::Function *fn = prototypeResult->fn;
        auto entryBlock = llvm::BasicBlock::Create(*ctx, "entry", fn);
        builder->SetInsertPoint(entryBlock);

        // set the named values for the parameters (before the body is generated)
        namedValues.clear();
        for (auto &arg : fn->args()) {
            llvm::AllocaInst *allocaInstance = createEntryBlockAlloca(fn, std::string(arg.getName()), arg.getType());
            builder->CreateStore(&arg, allocaInstance);
            namedValues[std::string(arg.getName())] = allocaInstance;
        }

        body->codegen(ctx, builder, moduleLLVM);

        // fill any empty blocks
        for (llvm::BasicBlock& block : *fn) {
            if (block.empty()) {
                builder->SetInsertPoint(&block);
                builder->CreateUnreachable();
            }
        }

        return nullptr;
    }
};

class ASTReturnStatement : public ASTNode {
    ASTNode *expression;

 public:
    explicit ASTReturnStatement(ASTNode *expression): expression(expression) {}
    ~ASTReturnStatement() {
        delete expression;
    }

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Return Statement:\n";
        expression->print(depth + 1);
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        std::unique_ptr<CodegenResult> expressionResult = expression->codegen(ctx, builder, moduleLLVM);
        if (expressionResult == nullptr || expressionResult->resultType != VALUE_CODEGEN_RESULT) return nullptr;
        builder->CreateRet(expressionResult->value);
        return nullptr;
    }
};

class ASTVariableDeclaration : public ASTNode {
    std::string identifier;
    std::string type;

 public:
    ASTVariableDeclaration(std::string identifier, std::string type): identifier(identifier), type(type) {}
    ~ASTVariableDeclaration() {}  // no child nodes

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Variable Declaration:\n";
        std::cout << std::string((depth + 1) * 2, ' ') << "Identifier: " << identifier << "\n";
        std::cout << std::string((depth + 1) * 2, ' ') << "Type: " << type << "\n";
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        if (namedValues[identifier] != nullptr) {
            generator::fatal_error(std::chrono::high_resolution_clock::now(), "Variable is already declared", "The variable '" + identifier + "' is already declared");
            return nullptr;
        }
        // create an allocation for the variable. Do it in the entry block so that it can get optimized easily
        llvm::AllocaInst *allocaInstance = createEntryBlockAlloca(builder->GetInsertBlock()->getParent(), identifier, getLLVMType(type, builder));
        namedValues[identifier] = allocaInstance;
        return nullptr;
    }
};

class ASTVariableAssignment : public ASTNode {
    std::string identifier;
    ASTNode *expression;

 public:
    ASTVariableAssignment(std::string identifier, ASTNode *expression): identifier(identifier), expression(expression) {}
    ~ASTVariableAssignment() {
        delete expression;
    }

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Variable Assignment:\n";
        std::cout << std::string((depth + 1) * 2, ' ') << "Identifier: " << identifier << "\n";
        expression->print(depth + 1);
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        if (namedValues[identifier] == nullptr) {
            generator::fatal_error(std::chrono::high_resolution_clock::now(), "Variable is not declared", "Cannot assign a value to the variable '" + identifier + "' since it has not been declared");
            return nullptr;
        }
        std::unique_ptr<CodegenResult> expressionResult = expression->codegen(ctx, builder, moduleLLVM);
        if (expressionResult == nullptr || expressionResult->resultType != VALUE_CODEGEN_RESULT) return nullptr;
        if (expressionResult->value->getType() != namedValues[identifier]->getAllocatedType()) {
            generator::fatal_error(std::chrono::high_resolution_clock::now(), "Type mismatch in variable assignment", "Cannot assign a value to the variable '" + identifier + "' which has a different type");
            return nullptr;
        }
        // store the value of the expression
        builder->CreateStore(expressionResult->value, namedValues[identifier]);
        return nullptr;
    }
};

class ASTVariableDefinition : public ASTNode {
    std::string identifier;
    std::string type;
    ASTNode *expression;

 public:
    ASTVariableDefinition(std::string identifier, std::string type, ASTNode *expression): identifier(identifier), type(type), expression(expression) {}
    ~ASTVariableDefinition() {
        delete expression;
    }

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Variable Definition:\n";
        std::cout << std::string((depth + 1) * 2, ' ') << "Identifier: " << identifier << "\n";
        std::cout << std::string((depth + 1) * 2, ' ') << "Type: " << identifier << "\n";
        expression->print(depth + 1);
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        if (namedValues[identifier] != nullptr) {
            generator::fatal_error(std::chrono::high_resolution_clock::now(), "Variable is already declared", "Cannot define the variable '" + identifier + "' since it is already declared");
            return nullptr;
        }
        std::unique_ptr<CodegenResult> expressionResult = expression->codegen(ctx, builder, moduleLLVM);
        if (expressionResult == nullptr || expressionResult->resultType != VALUE_CODEGEN_RESULT) return nullptr;
        // find the type of variable
        llvm::Type* variableType = type == "auto" ? expressionResult->value->getType() : getLLVMType(type, builder);
        if (expressionResult->value->getType() != variableType) {
            generator::fatal_error(std::chrono::high_resolution_clock::now(), "Type mismatch in variable definition", "The type of the expression does not match the type of definition for the variable '" + identifier + "'");
            return nullptr;
        }
        // allocate space for the variable and store the value of the expression
        llvm::AllocaInst *allocaInstance = createEntryBlockAlloca(builder->GetInsertBlock()->getParent(), identifier, variableType);
        namedValues[identifier] = allocaInstance;
        builder->CreateStore(expressionResult->value, allocaInstance);
        return nullptr;
    }
};

class ASTIfStatement : public ASTNode {
    ASTNode *expression;
    ASTNode *body;

 public:
    explicit ASTIfStatement(ASTNode *expression, ASTNode *body): expression(expression), body(body) {}
    ~ASTIfStatement() {
        delete expression;
        delete body;
    }

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "If Statement:\n";
        expression->print(depth + 1);
        body->print(depth + 1);
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        std::unique_ptr<CodegenResult> expressionResult = expression->codegen(ctx, builder, moduleLLVM);
        if (expressionResult == nullptr || expressionResult->resultType != VALUE_CODEGEN_RESULT) return nullptr;

        // get the boolean value of the expression
        llvm::Value *condition = getBooleanValue(expressionResult->value, builder);

        // create the branches
        llvm::Function *fn = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock *thenBlock = llvm::BasicBlock::Create(*ctx, "then", fn);
        llvm::BasicBlock *mergeBlock = llvm::BasicBlock::Create(*ctx, "ifcont", fn);

        // create conditional branch
        builder->CreateCondBr(condition, thenBlock, mergeBlock);

        // emit the "then" block
        builder->SetInsertPoint(thenBlock);
        std::unique_ptr<CodegenResult> thenResult = body->codegen(ctx, builder, moduleLLVM);
        if (!thenBlock->getTerminator()) builder->CreateBr(mergeBlock);
        // update the thenBlock since the codegen of thenBody might change the current block
        thenBlock = builder->GetInsertBlock();

        // emit the "merge" block
        builder->SetInsertPoint(mergeBlock);
        // TODO(anyone) may be needed in the future when mutable variables are added:
        // llvm::PHINode *phiNode = builder->CreatePHI(builder->getInt32Ty(), 2, "iftmp");
        // phiNode->addIncoming(thenValue, thenBlock);
        // phiNode->addIncoming(elseValue, elseBlock);

        return nullptr;
    }
};

class ASTIfElseStatement : public ASTNode {
    ASTNode *expression;
    ASTNode *thenBody;
    ASTNode *elseBody;

 public:
    explicit ASTIfElseStatement(ASTNode *expression, ASTNode *thenBody, ASTNode *elseBody): expression(expression), thenBody(thenBody), elseBody(elseBody) {}
    ~ASTIfElseStatement() {
        delete expression;
        delete thenBody;
        delete elseBody;
    }

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "If-else Statement:\n";
        expression->print(depth + 1);
        thenBody->print(depth + 1);
        elseBody->print(depth + 1);
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        std::unique_ptr<CodegenResult> expressionResult = expression->codegen(ctx, builder, moduleLLVM);
        if (expressionResult == nullptr || expressionResult->resultType != VALUE_CODEGEN_RESULT) return nullptr;
        // get the boolean value of the expression
        llvm::Value *condition = getBooleanValue(expressionResult->value, builder);

        // create the branches
        llvm::Function *fn = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock *thenBlock = llvm::BasicBlock::Create(*ctx, "then", fn);
        llvm::BasicBlock *elseBlock = llvm::BasicBlock::Create(*ctx, "else", fn);
        llvm::BasicBlock *mergeBlock = nullptr;

        // create conditional branch
        builder->CreateCondBr(condition, thenBlock, elseBlock);

        // emit the "then" block
        builder->SetInsertPoint(thenBlock);
        std::unique_ptr<CodegenResult> thenResult = thenBody->codegen(ctx, builder, moduleLLVM);
        llvm::Instruction *terminatorThenBlock = thenBlock->getTerminator();
        // if there is no return instruction then generate the mergeBlock and branch instruction
        if (terminatorThenBlock == nullptr || !isa<llvm::ReturnInst>(terminatorThenBlock)) {
            mergeBlock = llvm::BasicBlock::Create(*ctx, "ifcont", fn);
            builder->CreateBr(mergeBlock);
        }
        // update the thenBlock since the codegen of thenBody might change the current block
        thenBlock = builder->GetInsertBlock();

        // emit the "else" block
        builder->SetInsertPoint(elseBlock);
        std::unique_ptr<CodegenResult> elseResult = elseBody->codegen(ctx, builder, moduleLLVM);
        llvm::Instruction *terminatorElseBlock = elseBlock->getTerminator();
        // if there is no return instruction then generate the mergeBlock and branch instruction
        if (terminatorElseBlock == nullptr || !isa<llvm::ReturnInst>(terminatorElseBlock)) {
            if (!mergeBlock) mergeBlock = llvm::BasicBlock::Create(*ctx, "ifcont", fn);
            builder->CreateBr(mergeBlock);
        }
        // update the elseBlock since the codegen of elseBody might change the current block
        elseBlock = builder->GetInsertBlock();

        // emit the "merge" block if either the then or else blocks had a branch statement
        if (!mergeBlock) return nullptr;
        builder->SetInsertPoint(mergeBlock);
        // TODO(anyone) may be needed in the future when mutable variables are added:
        // llvm::PHINode *phiNode = builder->CreatePHI(builder->getInt32Ty(), 2, "iftmp");
        // phiNode->addIncoming(thenValue, thenBlock);
        // phiNode->addIncoming(elseValue, elseBlock);

        return nullptr;
    }
};

class ASTFunctionCall : public ASTNode {
    std::string identifier;
    std::vector<ASTNode*> argumentList;

 public:
    ASTFunctionCall(std::string identifier, std::vector<ASTNode*> argumentList): identifier(identifier), argumentList(argumentList) {}
    ~ASTFunctionCall() {
        // delete each node the vector
        for (ASTNode *argument : argumentList) {
            delete argument;
        }
    }

    void print(int depth) const override {
        std::cout << std::string(depth * 2, ' ') << "Function Call:\n";
        std::cout << std::string((depth + 1) * 2, ' ') << "Identifier: " << identifier << "\n";
        if (!argumentList.empty()) {
            std::cout << std::string((depth + 1) * 2, ' ') << "Arguments\n";
            // print each node the vector
            for (ASTNode *argument : argumentList) {
                argument->print(depth + 2);
            }
        } else {
            std::cout << std::string((depth + 1) * 2, ' ') << "No Arguments\n";
        }
    }

    std::unique_ptr<CodegenResult> codegen(const std::unique_ptr<llvm::LLVMContext>& ctx, const std::unique_ptr<llvm::IRBuilder<>>& builder, const std::unique_ptr<llvm::Module>& moduleLLVM) const override {
        llvm::Function *calleeFn = moduleLLVM->getFunction(identifier);
        if (!calleeFn) {
            generator::fatal_error(std::chrono::high_resolution_clock::now(), "Unknown function referenced", "The function '" + identifier + "' could not be found");
            return nullptr;
        }

        // if argument mismatch error.
        if (calleeFn->arg_size() != argumentList.size()) {
            generator::fatal_error(std::chrono::high_resolution_clock::now(), "Incorrect arguments passed", "The arguments passed do not match the parameters of function '" + identifier + "'");
            return nullptr;
        }

        std::vector<llvm::Value *> args;

        // generate the arguments
        for (ASTNode *argument : argumentList) {
            std::unique_ptr<CodegenResult> argumentResult = argument->codegen(ctx, builder, moduleLLVM);
            if (argumentResult == nullptr || argumentResult->resultType != VALUE_CODEGEN_RESULT) return nullptr;
            args.push_back(argumentResult->value);
        }

        return std::make_unique<CodegenResult>(builder->CreateCall(calleeFn, args, "calltmp"), VALUE_CODEGEN_RESULT);
    }
};
