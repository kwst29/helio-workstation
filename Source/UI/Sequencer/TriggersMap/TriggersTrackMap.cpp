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
#include "TriggersTrackMap.h"
#include "ProjectTreeItem.h"
#include "MidiSequence.h"
#include "AutomationSequence.h"
#include "PlayerThread.h"
#include "HybridRoll.h"
#include "TriggerEventComponent.h"
#include "TriggerEventConnector.h"
#include "MidiTrack.h"

#define DEFAULT_TRACKMAP_HEIGHT 16

TriggersTrackMap::TriggersTrackMap(ProjectTreeItem &parentProject, HybridRoll &parentRoll, WeakReference<MidiSequence> targetLayer) :
    project(parentProject),
    roll(parentRoll),
    layer(std::move(targetLayer)),
    projectFirstBeat(0.f),
    projectLastBeat(16.f),
    rollFirstBeat(0.f),
    rollLastBeat(16.f)
{
    this->setAlwaysOnTop(true);
    this->setPaintingIsUnclipped(true);

    this->leadingConnector = new TriggerEventConnector(nullptr, nullptr, DEFAULT_TRIGGER_AUTOMATION_EVENT_STATE);
    this->addAndMakeVisible(this->leadingConnector);
    
    this->setMouseCursor(MouseCursor::CopyingCursor);
    
    this->setInterceptsMouseClicks(true, true);
    this->setMouseClickGrabsKeyboardFocus(false);

    this->reloadTrack();
    
    this->project.addListener(this);

    this->setSize(1, DEFAULT_TRACKMAP_HEIGHT);
}

TriggersTrackMap::~TriggersTrackMap()
{
    this->project.removeListener(this);
}


//===----------------------------------------------------------------------===//
// Component
//===----------------------------------------------------------------------===//

void TriggersTrackMap::mouseDown(const MouseEvent &e)
{
    const bool shouldAddTriggeredEvent = (! e.mods.isLeftButtonDown());
    this->insertNewEventAt(e, shouldAddTriggeredEvent);
}

void TriggersTrackMap::resized()
{
    this->setVisible(false);
    
    // вместо одного updateSustainPedalComponent(с) -
    // во избежание глюков - скачала обновляем позиции
    for (int i = 0; i < this->eventComponents.size(); ++i)
    {
        TriggerEventComponent *const c = this->eventComponents.getUnchecked(i);
        c->setRealBounds(this->getEventBounds(c));
    }
    
    // затем - зависимые элементы
    for (int i = 0; i < this->eventComponents.size(); ++i)
    {
        TriggerEventComponent *const c = this->eventComponents.getUnchecked(i);
        c->updateConnector();
    }
    
    this->leadingConnector->resizeToFit(DEFAULT_TRIGGER_AUTOMATION_EVENT_STATE);
    
    this->setVisible(true);
}

void TriggersTrackMap::mouseWheelMove(const MouseEvent &event, const MouseWheelDetails &wheel)
{
    this->roll.mouseWheelMove(event.getEventRelativeTo(&this->roll), wheel);
}


Rectangle<float> TriggersTrackMap::getEventBounds(TriggerEventComponent *c) const
{
    return this->getEventBounds(c->getBeat(),
                                c->isPedalDownEvent(),
                                c->getAnchor());
}

static const float kComponentLengthInBeats = 0.5f;

Rectangle<float> TriggersTrackMap::getEventBounds(float targetBeat, bool isPedalDown, float anchor) const
{
    const float rollLengthInBeats = (this->rollLastBeat - this->rollFirstBeat);
    const float projectLengthInBeats = (this->projectLastBeat - this->projectFirstBeat);
    
    const float beat = (targetBeat - this->rollFirstBeat);
    const float mapWidth = float(this->getWidth()) * (projectLengthInBeats / rollLengthInBeats);
    
    const float x = (mapWidth * (beat / projectLengthInBeats));

    const float minWidth = 2.f;
    const float w = jmax(minWidth, (mapWidth * (kComponentLengthInBeats / projectLengthInBeats)));
    
    return Rectangle<float>(x - (w * anchor), 0.f, w, float(this->getHeight()));
}


//===----------------------------------------------------------------------===//
// Event Helpers
//===----------------------------------------------------------------------===//

void TriggersTrackMap::insertNewEventAt(const MouseEvent &e, bool shouldAddTriggeredEvent)
{
    const float rollLengthInBeats = (this->rollLastBeat - this->rollFirstBeat);
    const float projectLengthInBeats = (this->projectLastBeat - this->projectFirstBeat);
    const float mapWidth = float(this->getWidth()) * (projectLengthInBeats / rollLengthInBeats);
    const float w = mapWidth * (kComponentLengthInBeats / projectLengthInBeats);
    const float draggingBeat = this->getBeatByXPosition(int(e.x + w / 2));
    
    if (AutomationSequence *activeAutoLayer =
        dynamic_cast<AutomationSequence *>(this->layer.get()))
    {
        const AutomationEvent *firstEvent = static_cast<AutomationEvent *>(activeAutoLayer->getUnchecked(0));
        float prevEventCV = firstEvent->getControllerValue();
        float prevBeat = -FLT_MAX;
        float nextBeat = FLT_MAX;
        
        for (int i = 0; i < activeAutoLayer->size(); ++i)
        {
            const AutomationEvent *event = static_cast<AutomationEvent *>(activeAutoLayer->getUnchecked(i));
            prevEventCV = event->getControllerValue();
            prevBeat = event->getBeat();
            
            if (i < (activeAutoLayer->size() - 1))
            {
                const AutomationEvent *nextEvent = static_cast<AutomationEvent *>(activeAutoLayer->getUnchecked(i + 1));
                nextBeat = nextEvent->getBeat();
                
                if (event->getBeat() < draggingBeat &&
                    nextEvent->getBeat() > draggingBeat)
                {
                    break;
                }
            }
            else
            {
                nextBeat = FLT_MAX;
            }
        }
        
        const float invertedCV = (1.f - prevEventCV);
        const float alignedBeat = jmin((nextBeat - kComponentLengthInBeats), jmax((prevBeat + kComponentLengthInBeats), draggingBeat));
        
        activeAutoLayer->checkpoint();
        AutomationEvent event(activeAutoLayer, alignedBeat, invertedCV);
        activeAutoLayer->insert(event, true);
        
        if (shouldAddTriggeredEvent)
        {
            AutomationEvent triggerEvent(activeAutoLayer, alignedBeat + 0.75f, (1.f - invertedCV));
            activeAutoLayer->insert(triggerEvent, true);
        }
    }
}

void TriggersTrackMap::removeEventIfPossible(const AutomationEvent &e)
{
    AutomationSequence *autoLayer = static_cast<AutomationSequence *>(e.getSequence());
    
    if (autoLayer->size() > 1)
    {
        autoLayer->checkpoint();
        autoLayer->remove(e, true);
    }
}

TriggerEventComponent *TriggersTrackMap::getPreviousEventComponent(int indexOfSorted) const
{
    const int indexOfPrevious = indexOfSorted - 1;
    
    return
        isPositiveAndBelow(indexOfPrevious, this->eventComponents.size()) ?
        this->eventComponents.getUnchecked(indexOfPrevious) :
        nullptr;
}

TriggerEventComponent *TriggersTrackMap::getNextEventComponent(int indexOfSorted) const
{
    const int indexOfNext = indexOfSorted + 1;
    
    return
        isPositiveAndBelow(indexOfNext, this->eventComponents.size()) ?
        this->eventComponents.getUnchecked(indexOfNext) :
        nullptr;
}

float TriggersTrackMap::getBeatByXPosition(int x) const
{
    const int xRoll = int(roundf(float(x) / float(this->getWidth()) * float(this->roll.getWidth())));
    const float targetBeat = this->roll.getRoundBeatByXPosition(xRoll);
    return jmin(jmax(targetBeat, this->rollFirstBeat), this->rollLastBeat);
}


//===----------------------------------------------------------------------===//
// ProjectListener
//===----------------------------------------------------------------------===//

void TriggersTrackMap::onChangeMidiEvent(const MidiEvent &oldEvent, const MidiEvent &newEvent)
{
    if (newEvent.getSequence() == this->layer)
    {
        const AutomationEvent &autoEvent = static_cast<const AutomationEvent &>(oldEvent);
        const AutomationEvent &newAutoEvent = static_cast<const AutomationEvent &>(newEvent);
        
        if (TriggerEventComponent *component = this->eventsHash[autoEvent])
        {
            // update links and connectors
            this->eventComponents.sort(*component);
            const int indexOfSorted = this->eventComponents.indexOfSorted(*component, component);
            TriggerEventComponent *previousEventComponent(this->getPreviousEventComponent(indexOfSorted));
            TriggerEventComponent *nextEventComponent(this->getNextEventComponent(indexOfSorted));
            
            component->setNextNeighbour(nextEventComponent);
            component->setPreviousNeighbour(previousEventComponent);
            
            this->updateEventComponent(component);
            component->repaint();
            
            if (previousEventComponent)
            {
                previousEventComponent->setNextNeighbour(component);
                
                TriggerEventComponent *oneMorePrevious = this->getPreviousEventComponent(indexOfSorted - 1);
                previousEventComponent->setPreviousNeighbour(oneMorePrevious);
                
                if (oneMorePrevious)
                { oneMorePrevious->setNextNeighbour(previousEventComponent); }
            }
            
            if (nextEventComponent)
            {
                nextEventComponent->setPreviousNeighbour(component);

                TriggerEventComponent *oneMoreNext = this->getNextEventComponent(indexOfSorted + 1);
                nextEventComponent->setNextNeighbour(oneMoreNext);
                
                if (oneMoreNext)
                { oneMoreNext->setPreviousNeighbour(nextEventComponent); }
            }
            
            this->eventsHash.erase(autoEvent);
            this->eventsHash[newAutoEvent] = component;
            
            if (indexOfSorted == 0 || indexOfSorted == 1)
            {
                this->leadingConnector->retargetAndUpdate(nullptr, this->eventComponents[0], DEFAULT_TRIGGER_AUTOMATION_EVENT_STATE);
                // false - потому, что по умолчанию, с начала трека педаль не нажата
            }
        }
    }
}

void TriggersTrackMap::onAddMidiEvent(const MidiEvent &event)
{
    if (event.getSequence() == this->layer)
    {
        const AutomationEvent &autoEvent = static_cast<const AutomationEvent &>(event);
        
        auto component = new TriggerEventComponent(*this, autoEvent);
        this->addAndMakeVisible(component);
        
        // update links and connectors
        const int indexOfSorted = this->eventComponents.addSorted(*component, component);
        TriggerEventComponent *previousEventComponent(this->getPreviousEventComponent(indexOfSorted));
        TriggerEventComponent *nextEventComponent(this->getNextEventComponent(indexOfSorted));
        
        component->setNextNeighbour(nextEventComponent);
        component->setPreviousNeighbour(previousEventComponent);

        this->updateEventComponent(component);
        component->toFront(false);
        
        if (previousEventComponent)
        { previousEventComponent->setNextNeighbour(component); }

        if (nextEventComponent)
        { nextEventComponent->setPreviousNeighbour(component); }

        this->eventsHash[autoEvent] = component;
        
        if (indexOfSorted == 0)
        {
            this->leadingConnector->retargetAndUpdate(nullptr, this->eventComponents[0], DEFAULT_TRIGGER_AUTOMATION_EVENT_STATE);
        }
    }
}

void TriggersTrackMap::onRemoveMidiEvent(const MidiEvent &event)
{
    if (event.getSequence() == this->layer)
    {
        const AutomationEvent &autoEvent = static_cast<const AutomationEvent &>(event);
        
        if (TriggerEventComponent *component = this->eventsHash[autoEvent])
        {
            //this->eventAnimator.fadeOut(component, 150);
            this->removeChildComponent(component);
            this->eventsHash.erase(autoEvent);
            
            // update links and connectors for neighbors
            const int indexOfSorted = this->eventComponents.indexOfSorted(*component, component);
            TriggerEventComponent *previousEventComponent(this->getPreviousEventComponent(indexOfSorted));
            TriggerEventComponent *nextEventComponent(this->getNextEventComponent(indexOfSorted));
            
            if (previousEventComponent)
            { previousEventComponent->setNextNeighbour(nextEventComponent); }
            
            if (nextEventComponent)
            { nextEventComponent->setPreviousNeighbour(previousEventComponent); }
            
            this->eventComponents.removeObject(component, true);
            
            if (this->eventComponents.size() > 0)
            {
                this->leadingConnector->retargetAndUpdate(nullptr, this->eventComponents[0], DEFAULT_TRIGGER_AUTOMATION_EVENT_STATE);
            }
        }
    }
}

void TriggersTrackMap::onChangeTrackProperties(MidiTrack *const track)
{
    if (this->layer != nullptr && track->getSequence() == this->layer)
    {
        this->repaint();
    }
}

void TriggersTrackMap::onReloadProjectContent(const Array<MidiTrack *> &tracks)
{
    this->reloadTrack();
}

void TriggersTrackMap::onAddTrack(MidiTrack *const track)
{
    // TODO remove?
    if (this->layer != nullptr && track->getSequence() == this->layer)
    {
        this->reloadTrack();
    }
}

void TriggersTrackMap::onRemoveTrack(MidiTrack *const track)
{
    if (this->layer != nullptr && track->getSequence() == this->layer)
    {
        this->reloadTrack();
    }
}

void TriggersTrackMap::onChangeProjectBeatRange(float firstBeat, float lastBeat)
{
    this->projectFirstBeat = firstBeat;
    this->projectLastBeat = lastBeat;

    if (this->rollFirstBeat > firstBeat ||
        this->rollLastBeat < lastBeat)
    {
        this->rollFirstBeat = firstBeat;
        this->rollLastBeat = lastBeat;
        this->resized();
    }
}

void TriggersTrackMap::onChangeViewBeatRange(float firstBeat, float lastBeat)
{
    this->rollFirstBeat = firstBeat;
    this->rollLastBeat = lastBeat;
    this->resized();
}


//===----------------------------------------------------------------------===//
// Private
//===----------------------------------------------------------------------===//

void TriggersTrackMap::updateEventComponent(TriggerEventComponent *component)
{
    component->setRealBounds(this->getEventBounds(component));
    component->updateConnector();
}

void TriggersTrackMap::reloadTrack()
{
    //Logger::writeToLog("TriggersTrackMap::reloadSustainPedalTrack");
    
    for (int i = 0; i < this->eventComponents.size(); ++i)
    {
        this->removeChildComponent(this->eventComponents.getUnchecked(i));
    }
    
    this->eventComponents.clear();
    this->eventsHash.clear();
    
    this->setVisible(false);
    
    for (int j = 0; j < this->layer->size(); ++j)
    {
        MidiEvent *event = this->layer->getUnchecked(j);
        
        if (AutomationEvent *autoEvent = dynamic_cast<AutomationEvent *>(event))
        {
            auto component = new TriggerEventComponent(*this, *autoEvent);
            this->addAndMakeVisible(component);
            
            // update links and connectors
            const int indexOfSorted = this->eventComponents.addSorted(*component, component);
            TriggerEventComponent *previousEventComponent(this->getPreviousEventComponent(indexOfSorted));
            TriggerEventComponent *nextEventComponent(this->getNextEventComponent(indexOfSorted));
            
            component->setNextNeighbour(nextEventComponent);
            component->setPreviousNeighbour(previousEventComponent);

            //this->updateSustainPedalComponent(component); // double call? see resized() later
            
            if (previousEventComponent)
            { previousEventComponent->setNextNeighbour(component); }

            if (nextEventComponent)
            { nextEventComponent->setPreviousNeighbour(component); }

            this->eventsHash[*autoEvent] = component;
        }
    }
    
    if (this->eventComponents.size() > 0)
    {
        this->leadingConnector->retargetAndUpdate(nullptr, this->eventComponents[0], DEFAULT_TRIGGER_AUTOMATION_EVENT_STATE);
    }
    
    this->resized();
    this->setVisible(true);
}
