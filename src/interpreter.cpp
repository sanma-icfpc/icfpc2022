#include "stdafx.h"
#include "interpreter.h"
#include "similarity_checker.h"
#include <fmt/format.h>


InterpreterResult::InterpreterResult(const std::shared_ptr<Canvas>& canvas, const std::shared_ptr<OperandMap>& operands, int cost) : canvas(canvas), operands(operands), cost(cost) {}

std::shared_ptr<InterpreterResult> Interpreter::Run(CanvasPtr canvas, const std::vector<std::shared_ptr<Instruction>>& instructions, std::shared_ptr<OperandMap> operands)
{
  top_level_id_counter = canvas->calcTopLevelId();
  int totalCost = 0;
  for (const auto& inst : instructions) {
    const auto result = Interpret(canvas, operands, inst);
    canvas = result->canvas;
    totalCost += result->cost;
  }
  return std::make_shared<InterpreterResult>(canvas, operands, totalCost);
}

std::shared_ptr<InterpreterResult> Interpreter::Interpret(const std::shared_ptr<Canvas>& context, std::shared_ptr<OperandMap> operands, const std::shared_ptr<Instruction>& instruction) {
  if (auto color_instruction = std::dynamic_pointer_cast<ColorInstruction>(instruction)) return ColorCanvas(context, operands, color_instruction);
  else if (auto point_cut_instruction = std::dynamic_pointer_cast<PointCutInstruction>(instruction)) return PointCutCanvas(context, operands, point_cut_instruction);
  else if (auto vertical_cut_instruction = std::dynamic_pointer_cast<VerticalCutInstruction>(instruction)) return VerticalCutCanvas(context, operands, vertical_cut_instruction);
  else if (auto horizontal_cut_instruction = std::dynamic_pointer_cast<HorizontalCutInstruction>(instruction)) return HorizontalCutCanvas(context, operands, horizontal_cut_instruction);
  else if (auto swap_instruction = std::dynamic_pointer_cast<SwapInstruction>(instruction)) return SwapCanvas(context, operands, swap_instruction);
  else if (auto merge_instruction = std::dynamic_pointer_cast<MergeInstruction>(instruction)) return MergeCanvas(context, operands, merge_instruction);
  else if (auto nop_instruction = std::dynamic_pointer_cast<NopInstruction>(instruction)) {
    return std::make_shared<InterpreterResult>(context, operands, 0);
  }
  else if (auto comment_instruction = std::dynamic_pointer_cast<CommentInstruction>(instruction)) {
    return std::make_shared<InterpreterResult>(context, operands, 0);
  }
  else assert(false);
  return nullptr;
}

std::shared_ptr<InterpreterResult> Interpreter::ColorCanvas(const std::shared_ptr<Canvas>& context, std::shared_ptr<OperandMap>& operands, const std::shared_ptr<ColorInstruction>& color_instruction) {
  // TypeCheck Starts
  const auto& [blockId, color] = *color_instruction;
  assert_throw_invalid_instruction(context->blocks.count(blockId));
  auto block = context->blocks[blockId];
  // TypeCheck Ends

  // Scoring Starts
  const auto cost = getCost(
    *color_instruction,
    block->size.getScalarSize(),
    context->size().getScalarSize()
  );
  // Scoring Ends

  if (operands) {
    (*operands)[color_instruction] = {
      { block },
      { block },
    };
  }

  // Processing Starts
  if (block->typ == BlockType::SimpleBlockType) {
    const auto& actualBlock = std::dynamic_pointer_cast<SimpleBlock>(block);
    actualBlock->color = color;
    return std::make_shared<InterpreterResult>(context, operands, cost);
  }

  if (block->typ == BlockType::ComplexBlockType) {
    const auto& actualBlock = std::dynamic_pointer_cast<ComplexBlock>(block);
    context->blocks[blockId] = std::make_shared<SimpleBlock>(
      actualBlock->id,
      actualBlock->bottomLeft,
      actualBlock->topRight,
      color
      );
    return std::make_shared<InterpreterResult>(context, operands, cost);
  }
  // Processing Ends
  assert(false);
  return nullptr;
}

std::shared_ptr<InterpreterResult> Interpreter::PointCutCanvas(const std::shared_ptr<Canvas>& context, std::shared_ptr<OperandMap>& operands, const std::shared_ptr<PointCutInstruction>& point_cut_instruction) {
  // TypeCheck Starts
  const auto& [blockId, point] = *point_cut_instruction;
  assert_throw_invalid_instruction(context->blocks.count(blockId));
  auto block = context->blocks[blockId];
  assert_throw_invalid_instruction(point.isStrictlyInside(block->bottomLeft, block->topRight));
  // TypeCheck Ends

  // Scoring Starts
  const auto cost = getCost(
    *point_cut_instruction,
    block->size.getScalarSize(),
    context->size().getScalarSize()
  );
  // Scoring Ends

  // Processing Starts
  if (block->typ == BlockType::SimpleBlockType) {
    // 4 分割
    const auto& actualBlock = std::dynamic_pointer_cast<SimpleBlock>(block);
    const auto bottomLeftBlock = std::make_shared<SimpleBlock>(
      blockId + ".0",
      block->bottomLeft,
      point,
      actualBlock->color
      );
    const auto bottomRightBlock = std::make_shared<SimpleBlock>(
      blockId + ".1",
      Point(point.px, block->bottomLeft.py),
      Point(block->topRight.px, point.py),
      actualBlock->color
      );
    const auto topRightBlock = std::make_shared<SimpleBlock>(
      blockId + ".2",
      point,
      block->topRight,
      actualBlock->color
      );
    const auto topLeftBlock = std::make_shared<SimpleBlock>(
      blockId + ".3",
      Point(block->bottomLeft.px, point.py),
      Point(point.px, block->topRight.py),
      actualBlock->color
      );
    context->blocks.erase(blockId);
    context->blocks[blockId + ".0"] = bottomLeftBlock;
    context->blocks[blockId + ".1"] = bottomRightBlock;
    context->blocks[blockId + ".2"] = topRightBlock;
    context->blocks[blockId + ".3"] = topLeftBlock;

    if (operands) {
      (*operands)[point_cut_instruction] = {
        { block },
        { bottomLeftBlock, bottomRightBlock, topRightBlock, topLeftBlock },
      };
    }

    return std::make_shared<InterpreterResult>(context, operands, cost);
  }

  if (block->typ == BlockType::ComplexBlockType) {
    // マージされたブロック (complex block) に対して分割を行うような場合
    const auto& complexBlock = std::dynamic_pointer_cast<ComplexBlock>(block);
    std::vector<std::shared_ptr<SimpleBlock>> bottomLeftBlocks, bottomRightBlocks, topRightBlocks, topLeftBlocks;
    for (const auto& subBlock : complexBlock->subBlocks) {
      /**
      * __________________________
      * |        |       |       |
      * |   1    |   2   |   3   |
      * |________|_______|_______|
      * |        |       |       |
      * |   4    |   5   |  6    |
      * |________|_______|_______|
      * |        |       |       |
      * |   7    |   8   |   9   |
      * |________|_______|_______|
      */
      const auto& simpleBlock = std::dynamic_pointer_cast<SimpleBlock>(subBlock);
      // Case 3
      if (subBlock->bottomLeft.px >= point.px && subBlock->bottomLeft.py >= point.py) {
        topRightBlocks.push_back(subBlock);
        continue;
      }
      // Case 7
      if (subBlock->topRight.px <= point.px && subBlock->topRight.py <= point.py) {
        bottomLeftBlocks.push_back(subBlock);
        continue;
      }
      // Case 1
      if (subBlock->topRight.px <= point.px && subBlock->bottomLeft.py >= point.py) {
        topLeftBlocks.push_back(subBlock);
        continue;
      }
      // Case 9
      if (subBlock->bottomLeft.px >= point.px && subBlock->topRight.py <= point.py) {
        bottomRightBlocks.push_back(subBlock);
        continue;
      }
      // Case 5
      if (point.isStrictlyInside(subBlock->bottomLeft, subBlock->topRight)) {
        bottomLeftBlocks.push_back(std::make_shared<SimpleBlock>(
          "bl_child",
          subBlock->bottomLeft,
          point,
          simpleBlock->color
          ));
        bottomRightBlocks.push_back(std::make_shared<SimpleBlock>(
          "br_child",
          Point(point.px, subBlock->bottomLeft.py),
          Point(subBlock->topRight.px, point.py),
          simpleBlock->color
          ));
        topRightBlocks.push_back(std::make_shared<SimpleBlock>(
          "tr_child",
          point,
          subBlock->topRight,
          simpleBlock->color
          ));
        topLeftBlocks.push_back(std::make_shared<SimpleBlock>(
          "tl_child",
          Point(subBlock->bottomLeft.px, point.py),
          Point(point.px, subBlock->topRight.py),
          simpleBlock->color
          ));
        continue;
      }
      // Case 2
      if (subBlock->bottomLeft.px <= point.px
        && point.px <= subBlock->topRight.px
        && point.py <= subBlock->bottomLeft.py) {
        topLeftBlocks.push_back(std::make_shared<SimpleBlock>(
          "case2_tl_child",
          subBlock->bottomLeft,
          Point(point.px, subBlock->topRight.py),
          simpleBlock->color
          ));
        topRightBlocks.push_back(std::make_shared<SimpleBlock>(
          "case2_tr_child",
          Point(point.px, subBlock->bottomLeft.py),
          subBlock->topRight,
          simpleBlock->color
          ));
        continue;
      }
      // Case 8
      if (subBlock->bottomLeft.px <= point.px
        && point.px <= subBlock->topRight.px
        && point.py >= subBlock->topRight.py) {
        bottomLeftBlocks.push_back(std::make_shared<SimpleBlock>(
          "case8_bl_child",
          subBlock->bottomLeft,
          Point(point.px, subBlock->topRight.py),
          simpleBlock->color
          ));
        bottomRightBlocks.push_back(std::make_shared<SimpleBlock>(
          "case8_br_child",
          Point(point.px, subBlock->bottomLeft.py),
          subBlock->topRight,
          simpleBlock->color
          ));
        continue;
      }
      // Case 4
      if (subBlock->bottomLeft.py <= point.py
        && point.py <= subBlock->topRight.py
        && point.px <= subBlock->bottomLeft.px) {
        bottomRightBlocks.push_back(std::make_shared<SimpleBlock>(
          "case4_br_child",
          subBlock->bottomLeft,
          Point(subBlock->topRight.px, point.py),
          simpleBlock->color
          ));
        topRightBlocks.push_back(std::make_shared<SimpleBlock>(
          "case4_tr_child",
          Point(subBlock->bottomLeft.px, point.py),
          subBlock->topRight,
          simpleBlock->color
          ));
        continue;
      }
      // Case 6
      if (subBlock->bottomLeft.py <= point.py
        && point.py <= subBlock->topRight.py
        && point.px >= subBlock->topRight.px) {
        bottomLeftBlocks.push_back(std::make_shared<SimpleBlock>(
          "case6_bl_child",
          subBlock->bottomLeft,
          Point(subBlock->topRight.px, point.py),
          simpleBlock->color
          ));
        topLeftBlocks.push_back(std::make_shared<SimpleBlock>(
          "case6_br_child",
          Point(subBlock->bottomLeft.px, point.py),
          subBlock->topRight,
          simpleBlock->color
          ));
        continue;
      }
      assert(false);
    }
    const auto bottomLeftBlock = std::make_shared<ComplexBlock>(
      blockId + ".0",
      block->bottomLeft,
      point,
      bottomLeftBlocks
      );
    const auto bottomRightBlock = std::make_shared<ComplexBlock>(
      blockId + ".1",
      Point(point.px, block->bottomLeft.py),
      Point(block->topRight.px, point.py),
      bottomRightBlocks
      );
    const auto topRightBlock = std::make_shared<ComplexBlock>(
      blockId + ".2",
      point,
      block->topRight,
      topRightBlocks
      );
    const auto topLeftBlock = std::make_shared<ComplexBlock>(
      blockId + ".3",
      Point(block->bottomLeft.px, point.py),
      Point(point.px, block->topRight.py),
      topLeftBlocks
      );
    context->blocks.erase(blockId);
    context->blocks[blockId + ".0"] = bottomLeftBlock;
    context->blocks[blockId + ".1"] = bottomRightBlock;
    context->blocks[blockId + ".2"] = topRightBlock;
    context->blocks[blockId + ".3"] = topLeftBlock;

    if (operands) {
      (*operands)[point_cut_instruction] = {
        { block },
        { bottomLeftBlock, bottomRightBlock, topRightBlock, topLeftBlock },
      };
    }

    return std::make_shared<InterpreterResult>(context, operands, cost);
  }

  assert(false);
  return nullptr;
}

std::shared_ptr<InterpreterResult> Interpreter::VerticalCutCanvas(const std::shared_ptr<Canvas>& context, std::shared_ptr<OperandMap>& operands, const std::shared_ptr<VerticalCutInstruction>& vertical_cut_instruction) {
  // TypeCheck Starts
  const auto& [blockId, lineNumber] = *vertical_cut_instruction;
  assert_throw_invalid_instruction(context->blocks.count(blockId));
  auto block = context->blocks[blockId];
  assert_throw_invalid_instruction(block->bottomLeft.px <= lineNumber && lineNumber <= block->topRight.px);
  // TypeCheck Ends

  // Scoring Starts
  const auto cost = getCost(
    *vertical_cut_instruction,
    block->size.getScalarSize(),
    context->size().getScalarSize()
  );
  // Scoring Ends

  // Processing Starts
  if (block->typ == BlockType::SimpleBlockType) { // TODO: if (auto simpleBlock = std:dynamic_po...
    const auto& simpleBlock = std::dynamic_pointer_cast<SimpleBlock>(block);
    const auto leftBlock = std::make_shared<SimpleBlock>(
      blockId + ".0",
      block->bottomLeft,
      Point(lineNumber, block->topRight.py),
      simpleBlock->color
      );
    const auto rightBlock = std::make_shared<SimpleBlock>(
      blockId + ".1",
      Point(lineNumber, block->bottomLeft.py),
      block->topRight,
      simpleBlock->color
      );
    context->blocks.erase(blockId);
    context->blocks[blockId + ".0"] = leftBlock;
    context->blocks[blockId + ".1"] = rightBlock;

    if (operands) {
      (*operands)[vertical_cut_instruction] = {
        { block },
        { leftBlock, rightBlock },
      };
    }

    return std::make_shared<InterpreterResult>(context, operands, cost);
  }

  if (block->typ == BlockType::ComplexBlockType) {
    std::vector<std::shared_ptr<SimpleBlock>> leftBlocks, rightBlocks;
    const auto& complexBlock = std::dynamic_pointer_cast<ComplexBlock>(block);
    for (const auto& subBlock : complexBlock->subBlocks) {
      const auto& simpleBlock = std::dynamic_pointer_cast<SimpleBlock>(subBlock);
      if (subBlock->bottomLeft.px >= lineNumber) {
        rightBlocks.push_back(subBlock);
        continue;
      }
      if (subBlock->topRight.px <= lineNumber) {
        leftBlocks.push_back(subBlock);
        continue;
      }
      leftBlocks.push_back(std::make_shared<SimpleBlock>(
        "child",
        subBlock->bottomLeft,
        Point(lineNumber, subBlock->topRight.py),
        simpleBlock->color
        ));
      rightBlocks.push_back(std::make_shared<SimpleBlock>(
        "child",
        Point(lineNumber, subBlock->bottomLeft.py),
        subBlock->topRight,
        simpleBlock->color
        ));
    }
    const auto leftBlock = std::make_shared<ComplexBlock>(
      blockId + ".0",
      block->bottomLeft,
      Point(lineNumber, block->topRight.py),
      leftBlocks
      );
    const auto rightBlock = std::make_shared<ComplexBlock>(
      blockId + ".1",
      Point(lineNumber, block->bottomLeft.py),
      block->topRight,
      rightBlocks
      );
    context->blocks[blockId + ".0"] = leftBlock;
    context->blocks[blockId + ".1"] = rightBlock;

    if (operands) {
      (*operands)[vertical_cut_instruction] = {
        { block },
        { leftBlock, rightBlock },
      };
    }

    return std::make_shared<InterpreterResult>(context, operands, cost);
  }
  // Processing Ends

  assert(false);
  return nullptr;
}

std::shared_ptr<InterpreterResult> Interpreter::HorizontalCutCanvas(const std::shared_ptr<Canvas>& context, std::shared_ptr<OperandMap>& operands, const std::shared_ptr<HorizontalCutInstruction>& horizontal_cut_instruction) {
  // TypeCheck Starts
  const auto& [blockId, lineNumber] = *horizontal_cut_instruction;
  assert_throw_invalid_instruction(context->blocks.count(blockId));
  auto block = context->blocks[blockId];
  assert_throw_invalid_instruction(block->bottomLeft.py <= lineNumber && lineNumber <= block->topRight.py);
  // TypeCheck Ends

  // Scoring Starts
  const auto cost = getCost(
    *horizontal_cut_instruction,
    block->size.getScalarSize(),
    context->size().getScalarSize()
  );
  // Scoring Ends

  // Processing Starts
  if (block->typ == BlockType::SimpleBlockType) {
    const auto& simpleBlock = std::dynamic_pointer_cast<SimpleBlock>(block);
    const auto bottomBlock = std::make_shared<SimpleBlock>(
      blockId + ".0",
      block->bottomLeft,
      Point(block->topRight.px, lineNumber),
      simpleBlock->color
      );
    const auto topBlock = std::make_shared<SimpleBlock>(
      blockId + ".1",
      Point(block->bottomLeft.px, lineNumber),
      block->topRight,
      simpleBlock->color
      );
    context->blocks.erase(blockId);
    context->blocks[blockId + ".0"] = bottomBlock;
    context->blocks[blockId + ".1"] = topBlock;

    if (operands) {
      (*operands)[horizontal_cut_instruction] = {
        { block },
        { bottomBlock, topBlock },
      };
    }

    return std::make_shared<InterpreterResult>(context, operands, cost);
  }

  if (block->typ == BlockType::ComplexBlockType) {
    const auto& complexBlock = std::dynamic_pointer_cast<ComplexBlock>(block);
    std::vector<std::shared_ptr<SimpleBlock>> bottomBlocks, topBlocks;
    for (const auto& subBlock : complexBlock->subBlocks) {
      const auto& simpleBlock = std::dynamic_pointer_cast<SimpleBlock>(subBlock);
      if (subBlock->bottomLeft.py >= lineNumber) {
        topBlocks.push_back(subBlock);
        continue;
      }
      if (subBlock->topRight.py <= lineNumber) {
        bottomBlocks.push_back(subBlock);
        continue;
      }
      bottomBlocks.push_back(std::make_shared<SimpleBlock>(
        "child",
        subBlock->bottomLeft,
        Point(subBlock->topRight.px, lineNumber),
        simpleBlock->color
        ));
      topBlocks.push_back(std::make_shared<SimpleBlock>(
        "child",
        Point(subBlock->bottomLeft.px, lineNumber),
        subBlock->topRight,
        simpleBlock->color
        ));
    }
    context->blocks.erase(blockId);
    const auto bottomBlock = std::make_shared<ComplexBlock>(
      blockId + ".0",
      block->bottomLeft,
      Point(block->topRight.px, lineNumber),
      bottomBlocks
      );
    const auto topBlock = std::make_shared<ComplexBlock>(
      blockId + ".1",
      Point(block->bottomLeft.px, lineNumber),
      block->topRight,
      topBlocks
      );
    context->blocks[blockId + ".0"] = bottomBlock;
    context->blocks[blockId + ".1"] = topBlock;

    if (operands) {
      (*operands)[horizontal_cut_instruction] = {
        { block },
        { bottomBlock, topBlock },
      };
    }

    return std::make_shared<InterpreterResult>(context, operands, cost);
  }
  // Processing Ends

  assert(false);
  return nullptr;
}

std::shared_ptr<InterpreterResult> Interpreter::SwapCanvas(const std::shared_ptr<Canvas>& context, std::shared_ptr<OperandMap>& operands, const std::shared_ptr<SwapInstruction>& swap_instruction) {
  // TypeCheck Starts
  const auto& [blockId1, blockId2] = *swap_instruction;
  assert_throw_invalid_instruction(context->blocks.count(blockId1));
  assert_throw_invalid_instruction(context->blocks.count(blockId2));
  auto block1 = context->blocks[blockId1];
  auto block2 = context->blocks[blockId2];
  // TypeCheck Ends

  // Scoring Starts
  const auto cost = getCost(
    *swap_instruction,
    block1->size.getScalarSize(),
    context->size().getScalarSize()
  );
  // Scoring Ends

  // Processing Starts
  assert_throw_invalid_instruction(block1->size.px == block2->size.px && block1->size.py == block2->size.py);

  std::shared_ptr<Block> newBlock1, newBlock2;
  if (block1->typ == BlockType::SimpleBlockType) {
    newBlock2 = std::make_shared<SimpleBlock>(
      blockId1,
      block2->bottomLeft,
      block2->topRight,
      std::dynamic_pointer_cast<SimpleBlock>(block1)->color
      );
  }
  else {
    newBlock2 = std::make_shared<ComplexBlock>(
      blockId1,
      block2->bottomLeft,
      block2->topRight,
      std::dynamic_pointer_cast<ComplexBlock>(block1)->offsetChildren(block2->bottomLeft)
      );
  }
  if (block2->typ == BlockType::SimpleBlockType) {
    newBlock1 = std::make_shared<SimpleBlock>(
      blockId2,
      block1->bottomLeft,
      block1->topRight,
      std::dynamic_pointer_cast<SimpleBlock>(block2)->color
      );
  }
  else {
    newBlock1 = std::make_shared<ComplexBlock>(
      blockId2,
      block1->bottomLeft,
      block1->topRight,
      std::dynamic_pointer_cast<ComplexBlock>(block2)->offsetChildren(block1->bottomLeft)
      );
  }

  context->blocks[blockId1] = newBlock2;
  context->blocks[blockId2] = newBlock1;

  if (operands) {
    (*operands)[swap_instruction] = {
      { block1, block2 },
      { newBlock1, newBlock2 },
    };
  }

  return std::make_shared<InterpreterResult>(context, operands, cost);
  // Processing Ends
}

std::shared_ptr<InterpreterResult> Interpreter::MergeCanvas(const std::shared_ptr<Canvas>& context, std::shared_ptr<OperandMap>& operands, const std::shared_ptr<MergeInstruction>& merge_instruction) {
  // TypeCheck Starts
  const auto& [blockId1, blockId2] = *merge_instruction;
  assert_throw_invalid_instruction(context->blocks.count(blockId1));
  assert_throw_invalid_instruction(context->blocks.count(blockId2));
  auto block1 = context->blocks[blockId1];
  auto block2 = context->blocks[blockId2];
  // TypeCheck Ends

  // Scoring Starts
  const auto cost = getCost(
    *merge_instruction,
    std::max(block1->size.getScalarSize(), block2->size.getScalarSize()),
    context->size().getScalarSize()
  );
  // Scoring Ends

  // Processing Starts
  const auto bottomToTop =
    (block1->bottomLeft.py == block2->topRight.py || block1->topRight.py == block2->bottomLeft.py) &&
    block1->bottomLeft.px == block2->bottomLeft.px &&
    block1->topRight.px == block2->topRight.px;
  if (bottomToTop) {
    top_level_id_counter++;
    Point newBottomLeft, newTopRight;
    if (block1->bottomLeft.py < block2->bottomLeft.py) {
      newBottomLeft = block1->bottomLeft;
      newTopRight = block2->topRight;
    }
    else {
      newBottomLeft = block2->bottomLeft;
      newTopRight = block1->topRight;
    }
    std::vector<std::shared_ptr<SimpleBlock>> newBlocks(block1->getChildren());
    for (const auto& block : block2->getChildren()) newBlocks.push_back(block);
    const auto newBlock = std::make_shared<ComplexBlock>(
      std::to_string(top_level_id_counter),
      newBottomLeft,
      newTopRight,
      newBlocks
      );
    context->blocks[newBlock->id] = newBlock;
    context->blocks.erase(blockId1);
    context->blocks.erase(blockId2);

    if (operands) {
      (*operands)[merge_instruction] = {
        { block1, block2 },
        { newBlock },
      };
    }

    return std::make_shared<InterpreterResult>(context, operands, cost);
  }

  const auto leftToRight =
    (block1->bottomLeft.px == block2->topRight.px || block1->topRight.px == block2->bottomLeft.px) &&
    block1->bottomLeft.py == block2->bottomLeft.py &&
    block1->topRight.py == block2->topRight.py;
  if (leftToRight) {
    top_level_id_counter++;
    Point newBottomLeft, newTopRight;
    if (block1->bottomLeft.px < block2->bottomLeft.px) {
      newBottomLeft = block1->bottomLeft;
      newTopRight = block2->topRight;
    }
    else {
      newBottomLeft = block2->bottomLeft;
      newTopRight = block1->topRight;
    }
    std::vector<std::shared_ptr<SimpleBlock>> newBlocks(block1->getChildren());
    for (const auto& block : block2->getChildren()) newBlocks.push_back(block);
    const auto newBlock = std::make_shared<ComplexBlock>(
      std::to_string(top_level_id_counter),
      newBottomLeft,
      newTopRight,
      newBlocks
      );
    context->blocks[newBlock->id] = newBlock;
    context->blocks.erase(blockId1);
    context->blocks.erase(blockId2);

    if (operands) {
      (*operands)[merge_instruction] = {
        { block1, block2 },
        { newBlock },
      };
    }

    return std::make_shared<InterpreterResult>(context, operands, cost);
  }

  assert(bottomToTop || leftToRight);
  return nullptr;
}

std::optional<CostBreakdown> computeCost(const Painting& problem, const CanvasPtr& initial_canvas, const std::vector<std::shared_ptr<Instruction>>& instructions, std::shared_ptr<OperandMap> operands) {

  auto canvas = initial_canvas->Clone();

  Interpreter interpreter;
  std::shared_ptr<InterpreterResult> interpreter_result = interpreter.Run(canvas, instructions, operands);
  if (!interpreter_result) {
    LOG(ERROR) << fmt::format("failed to run the solution! terminating.");
    return std::nullopt;
  }
  const int instructions_cost = interpreter_result->cost;
  const int similarity_cost = SimilarityChecker::imageDiff(problem.frame, Painter::draw(*interpreter_result->canvas, false));
  const int total_cost = instructions_cost + similarity_cost;
  return CostBreakdown{ instructions_cost, similarity_cost, total_cost, interpreter_result->canvas };
}
