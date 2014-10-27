/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield
 *
 * This library is open source and may be redistributed and/or modified under
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * OpenSceneGraph Public License for more details.
*/

#include "SimpleDotVisitor.h"

namespace osgDot {

  SimpleDotVisitor::SimpleDotVisitor() {
  }

  SimpleDotVisitor::~SimpleDotVisitor() {
  }

  void SimpleDotVisitor::handle(osg::Node& node, int id) {
     std::cout << "- SimpleDotVisitor::handle NODE. ID = " << id << std::endl;
    std::stringstream label;
    label << "<top> Node";
    if ( !node.getName().empty() ) { label << "| " << node.getName(); }
    drawNode( id, "record", "solid", label.str(), "black", "white" );
  }

  void SimpleDotVisitor::handle(osg::Geode& node, int id) {
      std::cout << "- SimpleDotVisitor::handle GEODE. ID = " << id << std::endl;
    std::stringstream label;
    label << "<top> " << node.className();
    if ( !node.getName().empty() ) { label << "| " << node.getName(); }
    drawNode( id, "record", "solid", label.str(), "brown", "white" );
  }

  void SimpleDotVisitor::handle(osg::Group& node, int id) {
      std::cout << "- SimpleDotVisitor::handle GROUP. ID = " << id << std::endl;
    std::stringstream label;
    label << "<top> " << node.className();
    if ( !node.getName().empty() ) { label << "| " << node.getName(); }
    drawNode( id, "record", "solid", label.str(), "black", "white" );
  }

  void SimpleDotVisitor::handle(osg::Group& parent, osg::Node& child, int parentID, int childID ) {
      std::cout << "- SimpleDotVisitor::handle GROUP-NODE. ParentID = " << parentID << ", ChildID = " << childID << std::endl;
    drawEdge( parentID, childID, "setlinewidth(2)" );
  }

  void SimpleDotVisitor::handle(osg::StateSet& stateset, int id) {
      std::cout << "- SimpleDotVisitor::handle SSET. ID = " << id << std::endl;
    std::stringstream label;
    label << "<top> " << stateset.className();
    if ( !stateset.getName().empty() ) { label << "| " << stateset.getName(); }
    drawNode( id, "Mrecord", "solid", label.str(), "green", "white" );
  }

  void SimpleDotVisitor::handle(osg::Node& node, osg::StateSet& stateset, int parentID, int childID ) {
      std::cout << "- SimpleDotVisitor::handle NODE-SSET. ParentID = " << parentID << ", ChildID = " << childID << std::endl;
    drawEdge( parentID, childID, "dashed" );
  }

  void SimpleDotVisitor::handle(osg::Drawable& drawable, int id) {
      std::cout << "- SimpleDotVisitor::handle DRAWABLE. ID = " << id << std::endl;
    std::stringstream label;
    label << "<top> " << drawable.className();
    if ( !drawable.getName().empty() ) { label << "| " << drawable.getName(); }
    drawNode( id, "record", "solid", label.str(), "blue", "white" );
  }

  void SimpleDotVisitor::handle(osg::Geode& geode, osg::Drawable& drawable, int parentID, int childID ) {
      std::cout << "- SimpleDotVisitor::handle GEODE-DRAWABLE. ParentID = " << parentID << ". ChildID = " << childID << std::endl;
    drawEdge( parentID, childID, "dashed" );
  }

  void SimpleDotVisitor::handle(osg::Drawable& drawable, osg::StateSet& stateset, int parentID, int childID ) {
      std::cout << "- SimpleDotVisitor::handle DRAWABLE-SSET. ParentID = " << parentID << ", ChildID = " << childID << std::endl;
    drawEdge( parentID, childID, "dashed" );
  }

  void SimpleDotVisitor::drawNode( int id, const std::string& shape, const std::string& style, const std::string& label, const std::string& color, const std::string& fillColor ) {
      std::cout << "- SimpleDotVisitor::drawNode. ID = " << id << std::endl;
    _nodes << id <<
      "[shape=\"" << shape <<
      "\" ,label=\"" << label <<
      "\" ,style=\"" << style <<
      "\" ,color=\"" << color <<
      "\" ,fillColor=\"" << fillColor <<
      "\"]" << std::endl;
  }

  void SimpleDotVisitor::drawEdge( int sourceId, int sinkId, const std::string& style ) {
      std::cout << "- SimpleDotVisitor::handle drawEdge. SourceID = " << sourceId << ", TargetID = " << sinkId << std::endl;
    _edges
      << sourceId << ":top -> "
      << sinkId   << ":top [style=\""
      << style    << "\"];"
      << std::endl;
  }

} // namespace osgDot
