// Copyright (c) 2012-2021 Wojciech Figat. All rights reserved.

#if COMPILE_WITH_MATERIAL_GRAPH

#include "MaterialGenerator.h"
#include "Engine/Content/Assets/MaterialFunction.h"

void MaterialGenerator::ProcessGroupMaterial(Box* box, Node* node, Value& value)
{
    switch (node->TypeID)
    {
        // World Position
    case 2:
        value = Value(VariantType::Vector3, TEXT("input.WorldPosition.xyz"));
        break;
        // View
    case 3:
    {
        switch (box->ID)
        {
            // Position
        case 0:
            value = Value(VariantType::Vector3, TEXT("ViewPos"));
            break;
            // Direction
        case 1:
            value = Value(VariantType::Vector3, TEXT("ViewDir"));
            break;
            // Far Plane
        case 2:
            value = Value(VariantType::Float, TEXT("ViewFar"));
            break;
        default: CRASH;
        }
        break;
    }
        // Normal
    case 4:
        value = getNormal;
        break;
        // Camera Vector
    case 5:
        value = getCameraVector(node);
        break;
        // Screen Position
    case 6:
    {
        // Position
        if (box->ID == 0)
            value = Value(VariantType::Vector2, TEXT("input.SvPosition.xy"));
            // Texcoord
        else if (box->ID == 1)
            value = writeLocal(VariantType::Vector2, TEXT("input.SvPosition.xy * ScreenSize.zw"), node);

        break;
    }
        // Screen Size
    case 7:
    {
        value = Value(VariantType::Vector2, box->ID == 0 ? TEXT("ScreenSize.xy") : TEXT("ScreenSize.zw"));
        break;
    }
        // Custom code
    case 8:
    {
        // Skip if has no code
        if (((StringView)node->Values[0]).IsEmpty())
        {
            value = Value::Zero;
            break;
        }

        const int32 InputsMax = 8;
        const int32 OutputsMax = 4;
        const int32 Input0BoxID = 0;
        const int32 Output0BoxID = 8;

        // Create output variables
        Value values[OutputsMax];
        for (int32 i = 0; i < OutputsMax; i++)
        {
            const auto outputBox = node->GetBox(Output0BoxID + i);
            if (outputBox && outputBox->HasConnection())
            {
                values[i] = writeLocal(VariantType::Vector4, node);
            }
        }

        // Process custom code (inject inputs and outputs)
        String code;
        code = (StringView)node->Values[0];
        for (int32 i = 0; i < InputsMax; i++)
        {
            auto inputName = TEXT("Input") + StringUtils::ToString(i);
            const auto inputBox = node->GetBox(Input0BoxID + i);
            if (inputBox && inputBox->HasConnection())
            {
                auto inputValue = tryGetValue(inputBox, Value::Zero);
                if (inputValue.Type != VariantType::Vector4)
                    inputValue = inputValue.Cast(VariantType::Vector4);
                code.Replace(*inputName, *inputValue.Value, StringSearchCase::CaseSensitive);
            }
        }
        for (int32 i = 0; i < OutputsMax; i++)
        {
            auto outputName = TEXT("Output") + StringUtils::ToString(i);
            const auto outputBox = node->GetBox(Output0BoxID + i);
            if (outputBox && outputBox->HasConnection())
            {
                code.Replace(*outputName, *values[i].Value, StringSearchCase::CaseSensitive);
            }
        }

        // Write code
        _writer.Write(TEXT("{\n"));
        _writer.Write(*code);
        _writer.Write(TEXT("}\n"));

        // Link output values to boxes
        for (int32 i = 0; i < OutputsMax; i++)
        {
            const auto outputBox = node->GetBox(Output0BoxID + i);
            if (outputBox && outputBox->HasConnection())
            {
                outputBox->Cache = values[i];
            }
        }

        value = box->Cache;
        break;
    }
        // Object Position
    case 9:
        value = Value(VariantType::Vector3, TEXT("GetObjectPosition(input)"));
        break;
        // Two Sided Sign
    case 10:
        value = Value(VariantType::Float, TEXT("input.TwoSidedSign"));
        break;
        // Camera Depth Fade
    case 11:
    {
        auto faeLength = tryGetValue(node->GetBox(0), node->Values[0]).AsFloat();
        auto fadeOffset = tryGetValue(node->GetBox(1), node->Values[1]).AsFloat();

        // TODO: for pixel shader it could calc PixelDepth = mul(float4(WorldPos.xyz, 1), ViewProjMatrix).w and use it

        auto x1 = writeLocal(VariantType::Vector3, TEXT("ViewPos - input.WorldPosition"), node);
        auto x2 = writeLocal(VariantType::Vector3, TEXT("TransformViewVectorToWorld(input, float3(0, 0, -1))"), node);
        auto x3 = writeLocal(VariantType::Float, String::Format(TEXT("dot(normalize({0}), {1}) * length({0})"), x1.Value, x2.Value), node);
        auto x4 = writeLocal(VariantType::Float, String::Format(TEXT("{0} - {1}"), x3.Value, fadeOffset.Value), node);
        auto x5 = writeLocal(VariantType::Float, String::Format(TEXT("saturate({0} / {1})"), x4.Value, faeLength.Value), node);

        value = x5;
        break;
    }
        // Vertex Color
    case 12:
        value = getVertexColor;
        _treeLayer->UsageFlags |= MaterialUsageFlags::UseVertexColor;
        break;
        // Pre-skinned Local Position
    case 13:
        value = _treeType == MaterialTreeType::VertexShader ? Value(VariantType::Vector3, TEXT("input.PreSkinnedPosition")) : Value::Zero;
        break;
        // Pre-skinned Local Normal
    case 14:
        value = _treeType == MaterialTreeType::VertexShader ? Value(VariantType::Vector3, TEXT("input.PreSkinnedNormal")) : Value::Zero;
        break;
        // Depth
    case 15:
        value = writeLocal(VariantType::Float, TEXT("distance(ViewPos, input.WorldPosition)"), node);
        break;
        // Tangent
    case 16:
        value = Value(VariantType::Vector3, TEXT("input.TBN[0]"));
        break;
        // Bitangent
    case 17:
        value = Value(VariantType::Vector3, TEXT("input.TBN[1]"));
        break;
        // Camera Position
    case 18:
        value = Value(VariantType::Vector3, TEXT("ViewPos"));
        break;
        // Per Instance Random
    case 19:
        value = Value(VariantType::Float, TEXT("GetPerInstanceRandom(input)"));
        break;
        // Interpolate VS To PS
    case 20:
    {
        const auto input = node->GetBox(0);

        // If used in VS then pass the value from the input box
        if (_treeType == MaterialTreeType::VertexShader)
        {
            value = tryGetValue(input, Value::Zero).AsVector4();
            break;
        }

        // Check if can use more interpolants
        if (_vsToPsInterpolants.Count() == 16)
        {
            OnError(node, box, TEXT("Too many VS to PS interpolants used."));
            value = Value::Zero;
            break;
        }

        // Check if can use interpolants
        const auto layer = GetRootLayer();
        if (!layer || layer->Domain == MaterialDomain::Decal || layer->Domain == MaterialDomain::PostProcess)
        {
            OnError(node, box, TEXT("VS to PS interpolants are not supported in Decal or Post Process materials."));
            value = Value::Zero;
            break;
        }

        // Indicate the interpolator slot usage
        value = Value(VariantType::Vector4, String::Format(TEXT("input.CustomVSToPS[{0}]"), _vsToPsInterpolants.Count()));
        _vsToPsInterpolants.Add(input);
        break;
    }
        // Terrain Holes Mask
    case 21:
    {
        MaterialLayer* baseLayer = GetRootLayer();
        if (baseLayer->Domain == MaterialDomain::Terrain)
            value = Value(VariantType::Float, TEXT("input.HolesMask"));
        else
            value = Value::One;
        break;
    }
        // Terrain Layer Weight
    case 22:
    {
        MaterialLayer* baseLayer = GetRootLayer();
        if (baseLayer->Domain != MaterialDomain::Terrain)
        {
            value = Value::One;
            break;
        }

        const int32 layer = node->Values[0].AsInt;
        if (layer < 0 || layer > 7)
        {
            value = Value::One;
            OnError(node, box, TEXT("Invalid terrain layer index."));
            break;
        }

        const int32 slotIndex = layer / 4;
        const int32 componentIndex = layer % 4;
        value = Value(VariantType::Float, String::Format(TEXT("input.Layers[{0}][{1}]"), slotIndex, componentIndex));
        break;
    }
        // Depth Fade
    case 23:
    {
        // Calculate screen-space UVs
        auto screenUVs = writeLocal(VariantType::Vector2, TEXT("input.SvPosition.xy * ScreenSize.zw"), node);

        // Sample scene depth buffer
        auto sceneDepthTexture = findOrAddSceneTexture(MaterialSceneTextures::SceneDepth);
        auto depthSample = writeLocal(VariantType::Float, String::Format(TEXT("{0}.SampleLevel(SamplerLinearClamp, {1}, 0).x"), sceneDepthTexture.ShaderName, screenUVs.Value), node);

        // Linearize raw device depth
        Value sceneDepth;
        linearizeSceneDepth(node, depthSample, sceneDepth);

        // Calculate pixel depth
        auto posVS = writeLocal(VariantType::Float, TEXT("mul(float4(input.WorldPosition.xyz, 1), ViewMatrix).z"), node);

        // Compute depth difference
        auto depthDiff = writeLocal(VariantType::Float, String::Format(TEXT("{0} * ViewFar - {1}"), sceneDepth.Value, posVS.Value), node);

        // Apply smoothing factor and clamp the result
        value = writeLocal(VariantType::Float, String::Format(TEXT("saturate({0} / {1})"), depthDiff.Value, node->Values[0].AsFloat), node);
        break;
    }
        // Material Function
    case 24:
    {
        // Load function asset
        const auto function = Assets.LoadAsync<MaterialFunction>((Guid)node->Values[0]);
        if (!function || function->WaitForLoaded())
        {
            OnError(node, box, TEXT("Missing or invalid function."));
            value = Value::Zero;
            break;
        }

#if 0
        // Prevent recursive calls
        for (int32 i = _callStack.Count() - 1; i >= 0; i--)
        {
            if (_callStack[i]->Type == GRAPH_NODE_MAKE_TYPE(1, 24))
            {
                const auto callFunc = Assets.LoadAsync<MaterialFunction>((Guid)_callStack[i]->Values[0]);
                if (callFunc == function)
                {
                    OnError(node, box, String::Format(TEXT("Recursive call to function '{0}'!"), function->ToString()));
                    value = Value::Zero;
                    return;
                }
            }
        }
#endif

        // Create a instanced version of the function graph
        Graph* graph;
        if (!_functions.TryGet(node, graph))
        {
            graph = New<MaterialGraph>();
            function->LoadSurface((MaterialGraph&)*graph);
            _functions.Add(node, graph);
        }

        // Peek the function output (function->Outputs maps the functions outputs to output nodes indices)
        const int32 outputIndex = box->ID - 16;
        if (outputIndex < 0 || outputIndex >= function->Outputs.Count())
        {
            OnError(node, box, TEXT("Invalid function output box."));
            value = Value::Zero;
            break;
        }
        Node* functionOutputNode = &graph->Nodes[function->Outputs[outputIndex]];
        Box* functionOutputBox = functionOutputNode->TryGetBox(0);

        // Evaluate the function output
        _graphStack.Push(graph);
        value = functionOutputBox && functionOutputBox->HasConnection() ? eatBox(node, functionOutputBox->FirstConnection()) : Value::Zero;
        _graphStack.Pop();
        break;
    }
        // Object Size
    case 25:
        value = Value(VariantType::Vector3, TEXT("GetObjectSize(input)"));
        break;
    case 48:
    {
        const auto BaseNormal = tryGetValue(node->GetBox(0), Value::Zero).AsVector3();
        const auto AdditionalNormal = tryGetValue(node->GetBox(1), Value::Zero).AsVector3();
        const String text = String::Format(TEXT("float3(0.5, 0.5, 0.5)"), BaseNormal.Value, AdditionalNormal.Value);
        value = writeLocal(ValueType::Vector3, text, node);
        break;
    }
    default:
        break;
    }
}

void MaterialGenerator::ProcessGroupFunction(Box* box, Node* node, Value& value)
{
    switch (node->TypeID)
    {
        // Function Input
    case 1:
    {
        // Find the function call
        Node* functionCallNode = nullptr;
        ASSERT(_graphStack.Count() >= 2);
        Graph* graph;
        for (int32 i = _callStack.Count() - 1; i >= 0; i--)
        {
            if (_callStack[i]->Type == GRAPH_NODE_MAKE_TYPE(1, 24) && _functions.TryGet(_callStack[i], graph) && _graphStack[_graphStack.Count() - 1] == graph)
            {
                functionCallNode = _callStack[i];
                break;
            }
        }
        if (!functionCallNode)
        {
            OnError(node, box, TEXT("Missing calling function node."));
            value = Value::Zero;
            break;
        }
        const auto function = Assets.LoadAsync<MaterialFunction>((Guid)functionCallNode->Values[0]);
        if (!_functions.TryGet(functionCallNode, graph) || !function)
        {
            OnError(node, box, TEXT("Missing calling function graph."));
            value = Value::Zero;
            break;
        }

        // Peek the input box to use
        int32 inputIndex = -1;
        for (int32 i = 0; i < function->Inputs.Count(); i++)
        {
            if (node->ID == graph->Nodes[function->Inputs[i]].ID)
            {
                inputIndex = i;
                break;
            }
        }
        if (inputIndex < 0 || inputIndex >= function->Inputs.Count())
        {
            OnError(node, box, TEXT("Invalid function input box."));
            value = Value::Zero;
            break;
        }
        Box* functionCallBox = functionCallNode->TryGetBox(inputIndex);
        if (functionCallBox->HasConnection())
        {
            // Use provided input value from the function call
            _graphStack.Pop();
            value = eatBox(node, functionCallBox->FirstConnection());
            _graphStack.Push(graph);
        }
        else
        {
            // Use the default value from the function graph
            value = tryGetValue(node->TryGetBox(1), Value::Zero);
        }
        break;
    }
    default:
        break;
    }
}

#endif
