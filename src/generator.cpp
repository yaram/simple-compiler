#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include "list.h"
#include "types.h"

void string_buffer_append(char **string_buffer, const char *string) {
    auto string_length = strlen(*string_buffer);

    if(*string_buffer == nullptr) {
        *string_buffer = (char*)malloc(string_length + 1);

        strcpy(*string_buffer, string);
    } else {
        auto string_buffer_length = strlen(*string_buffer);

        auto new_string_buffer_length = string_buffer_length + string_length;

        auto new_string_buffer = (char*)realloc((void*)(*string_buffer), new_string_buffer_length + 1);

        strcpy(new_string_buffer, *string_buffer);

        strcat(new_string_buffer, string);

        *string_buffer = new_string_buffer;
    }
}

enum struct DeclarationCategory {
    FunctionDefinition
};

struct Declaration {
    DeclarationCategory category;

    const char *name;

    bool type_resolved;
    Type type;

    union{ 
        struct {
            Declaration *declarations;
            size_t declaration_count;

            Statement *statements;
            size_t statement_count;
        } function_definition;
    };
};

struct CreateDeclarationResult {
    bool is_declaration;

    Declaration declaration;
};

CreateDeclarationResult create_declaration(Statement statement) {
    switch(statement.type) {
        case StatementType::FunctionDefinition: {
            List<Declaration> child_declarations{};
            List<Statement> child_statements{};

            for(size_t i = 0; i < statement.function_definition.statement_count; i += 1) {
                auto child_statement = statement.function_definition.statements[i];
                auto result = create_declaration(child_statement);

                if(result.is_declaration){
                    append(&child_declarations, result.declaration);
                } else {
                    append(&child_statements, child_statement);
                }
            }

            Declaration declaration {
                DeclarationCategory::FunctionDefinition,
                statement.function_definition.name
            };

            declaration.function_definition = {
                child_declarations.elements,
                child_declarations.count,
                child_statements.elements,
                child_statements.count
            };

            return {
                true,
                declaration
            };
        } break;

        default: {
            return { false };
        } break;
    }
}

struct DeclarationTypeResolutionContext {
    List<Declaration> declaration_stack;

    Declaration *top_level_declarations;
    size_t top_level_declaration_count;
};

struct ResolveDeclarationTypeResult {
    bool status;

    Type type;
};

ResolveDeclarationTypeResult resolve_declaration_type(Declaration declaration, bool print_errors) {
    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            auto child_declarations_resolved = true;

            for(size_t i = 0; i < declaration.function_definition.declaration_count; i += 1) {
                auto child_declaration = declaration.function_definition.declarations[i];
            }

            if(!child_declarations_resolved) {
                return { false };
            }

            Type type;
            type.category = TypeCategory::Function;

            return {
                true,
                type
            };
        } break;

        default: {
            abort();
        } break;
    }
}

int count_resolved_declarations(Declaration declaration) {
    auto resolved_declaration_count = 0;

    if(declaration.type_resolved) {
        resolved_declaration_count = 1;
    }

    switch(declaration.category) {
        case DeclarationCategory::FunctionDefinition: {
            for(size_t i = 0; i < declaration.function_definition.declaration_count; i += 1) {
                auto child_declaration = declaration.function_definition.declarations[i];

                resolved_declaration_count += count_resolved_declarations(child_declaration);
            }
        } break;
    }

    return resolved_declaration_count;
}

GenerateCSourceResult generate_c_source(Statement *top_level_statements, size_t top_level_statement_count) {
    List<Declaration> top_level_declarations{};

    for(size_t i = 0; i < top_level_statement_count; i += 1) {
        auto result = create_declaration(top_level_statements[i]);

        if(result.is_declaration) {
            append(&top_level_declarations, result.declaration);
        } else {
            fprintf(stderr, "Only declarations are allowed in global scope");

            return { false };
        }
    }

    auto previous_resolved_declaration_count = 0;

    while(true) {
        for(size_t i = 0; i < top_level_declarations.count; i += 1) {
            auto top_level_declaration = &(top_level_declarations.elements[i]);

            if(!top_level_declaration->type_resolved) {
                auto result = resolve_declaration_type(*top_level_declaration, false);

                if(result.status) {
                    top_level_declaration->type_resolved = true;
                    top_level_declaration->type = result.type;
                }
            }
        }

        auto resolved_declaration_count = 0;
        
        for(size_t i = 0; i < top_level_declarations.count; i += 1) {
            auto declaration = top_level_declarations.elements[i];

            resolved_declaration_count += count_resolved_declarations(declaration);
        }

        if(resolved_declaration_count == previous_resolved_declaration_count) {
            for(size_t i = 0; i < top_level_declarations.count; i += 1) {
                auto top_level_declaration = top_level_declarations.elements[i];

                if(!top_level_declaration.type_resolved) {
                    auto result = resolve_declaration_type(top_level_declaration, true);

                    if(!result.status) {
                        return { false };
                    }
                }
            }

            break;
        }

        previous_resolved_declaration_count = resolved_declaration_count;
    }

    return {
        true,
        ""
    };
}