/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "PatternActions.h"
#include "MidiTrackSource.h"
#include "MidiSequence.h"
#include "TreeItem.h"
#include "SerializationKeys.h"

//===----------------------------------------------------------------------===//
// Insert Clip
//===----------------------------------------------------------------------===//

PatternClipInsertAction::PatternClipInsertAction(MidiTrackSource &source,
    String trackId, const Clip &target) noexcept :
    UndoAction(source),
    trackId(std::move(trackId)),
    clip(target) {}

bool PatternClipInsertAction::perform()
{
    if (Pattern *pattern = 
        this->source.findPatternByTrackId(this->trackId))
    {
        return pattern->insert(this->clip, false);
    }

    return false;
}

bool PatternClipInsertAction::undo()
{
    if (Pattern *pattern =
        this->source.findPatternByTrackId(this->trackId))
    {
        return pattern->remove(this->clip, false);
    }

    return false;
}

int PatternClipInsertAction::getSizeInUnits()
{
    return sizeof(Clip);
}

ValueTree PatternClipInsertAction::serialize() const
{
    ValueTree tree(Serialization::Undo::patternClipInsertAction);
    tree.setProperty(Serialization::Undo::trackId, this->trackId, nullptr);
    tree.appendChild(this->clip.serialize(), nullptr);
    return tree;
}

void PatternClipInsertAction::deserialize(const ValueTree &tree)
{
    this->trackId = tree.getProperty(Serialization::Undo::trackId);
    this->clip.deserialize(tree.getChild(0));
}

void PatternClipInsertAction::reset()
{
    this->clip.reset();
    this->trackId.clear();
}

//===----------------------------------------------------------------------===//
// Remove Instance
//===----------------------------------------------------------------------===//

PatternClipRemoveAction::PatternClipRemoveAction(MidiTrackSource &source,
    String trackId, const Clip &target) noexcept :
    UndoAction(source),
    trackId(std::move(trackId)),
    clip(target) {}

bool PatternClipRemoveAction::perform()
{
    if (Pattern *pattern =
        this->source.findPatternByTrackId(this->trackId))
    {
        return pattern->remove(this->clip, false);
    }

    return false;
}

bool PatternClipRemoveAction::undo()
{
    if (Pattern *pattern =
        this->source.findPatternByTrackId(this->trackId))
    {
        return pattern->insert(this->clip, false);
    }

    return false;
}

int PatternClipRemoveAction::getSizeInUnits()
{
    return sizeof(Clip);
}

ValueTree PatternClipRemoveAction::serialize() const
{
    ValueTree tree(Serialization::Undo::patternClipRemoveAction);
    tree.setProperty(Serialization::Undo::trackId, this->trackId, nullptr);
    tree.appendChild(this->clip.serialize(), nullptr);
    return tree;
}

void PatternClipRemoveAction::deserialize(const ValueTree &tree)
{
    this->trackId = tree.getProperty(Serialization::Undo::trackId);
    this->clip.deserialize(tree.getChild(0));
}

void PatternClipRemoveAction::reset()
{
    this->clip.reset();
    this->trackId.clear();
}

//===----------------------------------------------------------------------===//
// Change Instance
//===----------------------------------------------------------------------===//

PatternClipChangeAction::PatternClipChangeAction(MidiTrackSource &source,
    String trackId, const Clip &target, const Clip &newParameters) noexcept :
    UndoAction(source),
    trackId(std::move(trackId)),
    clipBefore(target),
    clipAfter(newParameters)
{
    jassert(target.getId() == newParameters.getId());
}

bool PatternClipChangeAction::perform()
{
    if (Pattern *pattern =
        this->source.findPatternByTrackId(this->trackId))
    {
        return pattern->change(this->clipBefore, this->clipAfter, false);
    }

    return false;
}

bool PatternClipChangeAction::undo()
{
    if (Pattern *pattern =
        this->source.findPatternByTrackId(this->trackId))
    {
        return pattern->change(this->clipAfter, this->clipBefore, false);
    }

    return false;
}

int PatternClipChangeAction::getSizeInUnits()
{
    return sizeof(Clip) * 2;
}

UndoAction *PatternClipChangeAction::createCoalescedAction(UndoAction *nextAction)
{
    if (Pattern *pattern = 
        this->source.findPatternByTrackId(this->trackId))
    {
        if (PatternClipChangeAction *nextChanger =
            dynamic_cast<PatternClipChangeAction *>(nextAction))
        {
            const bool idsAreEqual =
                (this->clipBefore.getId() == nextChanger->clipAfter.getId() &&
                this->trackId == nextChanger->trackId);

            if (idsAreEqual)
            {
                return new PatternClipChangeAction(this->source,
                    this->trackId, this->clipBefore, nextChanger->clipAfter);
            }
        }
    }

    (void)nextAction;
    return nullptr;
}

ValueTree PatternClipChangeAction::serialize() const
{
    ValueTree tree(Serialization::Undo::patternClipChangeAction);
    tree.setProperty(Serialization::Undo::trackId, this->trackId, nullptr);

    ValueTree instanceBeforeChild(Serialization::Undo::instanceBefore);
    instanceBeforeChild.appendChild(this->clipBefore.serialize(), nullptr);
    tree.appendChild(instanceBeforeChild, nullptr);

    ValueTree instanceAfterChild(Serialization::Undo::instanceAfter);
    instanceAfterChild.appendChild(this->clipAfter.serialize(), nullptr);
    tree.appendChild(instanceAfterChild, nullptr);

    return tree;
}

void PatternClipChangeAction::deserialize(const ValueTree &tree)
{
    this->trackId = tree.getProperty(Serialization::Undo::trackId);

    auto instanceBeforeChild = tree.getChildWithName(Serialization::Undo::instanceBefore);
    auto instanceAfterChild = tree.getChildWithName(Serialization::Undo::instanceAfter);

    this->clipBefore.deserialize(instanceBeforeChild.getChild(0));
    this->clipAfter.deserialize(instanceAfterChild.getChild(0));
}

void PatternClipChangeAction::reset()
{
    this->clipBefore.reset();
    this->clipAfter.reset();
    this->trackId.clear();
}
