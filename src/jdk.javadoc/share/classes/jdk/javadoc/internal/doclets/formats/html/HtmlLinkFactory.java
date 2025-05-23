/*
 * Copyright (c) 2003, 2024, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package jdk.javadoc.internal.doclets.formats.html;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Set;

import javax.lang.model.element.AnnotationMirror;
import javax.lang.model.element.Element;
import javax.lang.model.element.ExecutableElement;
import javax.lang.model.element.TypeElement;
import javax.lang.model.element.TypeParameterElement;
import javax.lang.model.type.ArrayType;
import javax.lang.model.type.DeclaredType;
import javax.lang.model.type.TypeMirror;
import javax.lang.model.type.TypeVariable;
import javax.lang.model.type.WildcardType;
import javax.lang.model.util.SimpleTypeVisitor14;

import jdk.javadoc.internal.doclets.formats.html.markup.HtmlStyles;
import jdk.javadoc.internal.doclets.toolkit.BaseConfiguration;
import jdk.javadoc.internal.doclets.toolkit.Resources;
import jdk.javadoc.internal.doclets.toolkit.util.DocPath;
import jdk.javadoc.internal.doclets.toolkit.util.DocPaths;
import jdk.javadoc.internal.doclets.toolkit.util.Utils;
import jdk.javadoc.internal.doclets.toolkit.util.Utils.ElementFlag;
import jdk.javadoc.internal.html.Content;
import jdk.javadoc.internal.html.ContentBuilder;
import jdk.javadoc.internal.html.Entity;
import jdk.javadoc.internal.html.HtmlId;
import jdk.javadoc.internal.html.HtmlTree;
import jdk.javadoc.internal.html.Text;

/**
 * A factory that returns a link given the information about it.
 */
public class HtmlLinkFactory {

    private final HtmlDocletWriter m_writer;
    private final DocPaths docPaths;
    private final Utils utils;

    /**
     * Constructs a new link factory.
     *
     * @param writer the HTML doclet writer
     */
    public HtmlLinkFactory(HtmlDocletWriter writer) {
        m_writer = writer;
        docPaths = writer.configuration.docPaths;
        utils = writer.configuration.utils;
    }

    /**
     * {@return a new instance of a content object}
     */
    protected Content newContent() {
        return new ContentBuilder();
    }

    /**
     * Constructs a link from the given link information.
     *
     * @param linkInfo the information about the link.
     * @return the link.
     */
    public Content getLink(HtmlLinkInfo linkInfo) {
        if (linkInfo.getType() != null) {
            SimpleTypeVisitor14<Content, HtmlLinkInfo> linkVisitor = new SimpleTypeVisitor14<>() {

                final Content link = newContent();

                // handles primitives, no types and error types
                @Override
                protected Content defaultAction(TypeMirror type, HtmlLinkInfo linkInfo) {
                    link.add(getTypeAnnotationLinks(linkInfo));
                    link.add(utils.getTypeSignature(type, false, false));
                    return link;
                }

                @Override
                public Content visitArray(ArrayType type, HtmlLinkInfo linkInfo) {
                    // int @A [] @B [] has @A on int[][] and @B on int[],
                    // encounter order is @A @B so print in FIFO order
                    var deque = new ArrayDeque<ArrayType>(1);
                    while (true) {
                        deque.add(type);
                        var component = type.getComponentType();
                        if (component instanceof ArrayType arrayType) {
                            type = arrayType;
                        } else {
                            visit(component, linkInfo.forType(component));
                            break;
                        }
                    }

                    while (!deque.isEmpty()) {
                        var currentType = deque.remove();
                        if (utils.isAnnotated(currentType)) {
                            link.add(" ");
                            link.add(getTypeAnnotationLinks(linkInfo.forType(currentType)));
                        }

                        // use vararg if required
                        if (linkInfo.isVarArg() && deque.isEmpty()) {
                            link.add("...");
                        } else {
                            link.add("[]");
                        }
                    }
                    return link;
                }

                @Override
                public Content visitWildcard(WildcardType type, HtmlLinkInfo linkInfo) {
                    link.add(getTypeAnnotationLinks(linkInfo));
                    link.add("?");
                    TypeMirror extendsBound = type.getExtendsBound();
                    if (extendsBound != null) {
                        link.add(" extends ");
                        link.add(getLink(getBoundsLinkInfo(linkInfo, extendsBound)));
                    }
                    TypeMirror superBound = type.getSuperBound();
                    if (superBound != null) {
                        link.add(" super ");
                        link.add(getLink(getBoundsLinkInfo(linkInfo, superBound)));
                    }
                    return link;
                }

                @Override
                public Content visitTypeVariable(TypeVariable type, HtmlLinkInfo linkInfo) {
                    link.add(getTypeAnnotationLinks(linkInfo));
                    TypeVariable typevariable = (utils.isArrayType(type))
                            ? (TypeVariable) utils.getComponentType(type)
                            : type;
                    Element owner = typevariable.asElement().getEnclosingElement();
                    if (linkInfo.linkTypeParameters() && utils.isTypeElement(owner)) {
                        linkInfo.setTypeElement((TypeElement) owner);
                        if (linkInfo.getLabel() == null || linkInfo.getLabel().isEmpty()) {
                            Content label = newContent();
                            label.add(utils.getTypeName(type, false));
                            linkInfo.label(label).skipPreview(true);
                        }
                        link.add(getClassLink(linkInfo));
                    } else {
                        // No need to link method type parameters.
                        link.add(utils.getTypeName(typevariable, false));
                    }

                    if (linkInfo.showTypeBounds()) {
                        linkInfo.showTypeBounds(false);
                        TypeParameterElement tpe = ((TypeParameterElement) typevariable.asElement());
                        boolean more = false;
                        List<? extends TypeMirror> bounds = utils.getBounds(tpe);
                        for (TypeMirror bound : bounds) {
                            // we get everything as extends java.lang.Object we suppress
                            // all of them except those that have multiple extends
                            if (bounds.size() == 1 &&
                                    utils.typeUtils.isSameType(bound, utils.getObjectType()) &&
                                    !utils.isAnnotated(bound)) {
                                continue;
                            }
                            link.add(more ? " & " : " extends ");
                            link.add(getLink(getBoundsLinkInfo(linkInfo, bound)));
                            more = true;
                        }
                    }
                    return link;
                }

                @Override
                public Content visitDeclared(DeclaredType type, HtmlLinkInfo linkInfo) {
                    TypeMirror enc = type.getEnclosingType();
                    if (enc instanceof DeclaredType dt && utils.isGenericType(dt)) {
                        // If an enclosing type has type parameters render them as separate links as
                        // otherwise this information is lost. On the other hand, plain enclosing types
                        // are not linked separately as they are easy to reach from the nested type.
                        visitDeclared(dt, linkInfo.forType(dt));
                        link.add(".");
                    }
                    link.add(getTypeAnnotationLinks(linkInfo));
                    linkInfo.setTypeElement(utils.asTypeElement(type));
                    link.add(getClassLink(linkInfo));
                    if (linkInfo.showTypeParameters()) {
                        link.add(getTypeParameterLinks(linkInfo));
                    }
                    return link;
                }
            };
            return linkVisitor.visit(linkInfo.getType(), linkInfo);
        } else if (linkInfo.getTypeElement() != null) {
            Content link = newContent();
            link.add(getClassLink(linkInfo));
            if (linkInfo.showTypeParameters()) {
                link.add(getTypeParameterLinks(linkInfo));
            }
            return link;
        } else {
            return null;
        }
    }

    /**
     * Returns a link to the given class.
     *
     * @param linkInfo the information about the link to construct
     * @return the link for the given class.
     */
    protected Content getClassLink(HtmlLinkInfo linkInfo) {
        BaseConfiguration configuration = m_writer.configuration;
        TypeElement typeElement = linkInfo.getTypeElement();
        // Create a tool tip if we are linking to a class or interface, or one of
        // its summary sections. Don't create one if we are linking to a member.
        boolean isPageOrSummaryLink = linkInfo.isPageOrSummaryLink();
        TypeMirror type = linkInfo.getType();
        if (type != null && utils.isTypeVariable(utils.getComponentType(type))) {
            linkInfo.fragment(m_writer.configuration.htmlIds.forTypeParam(
                    utils.getTypeName(utils.getComponentType(type), false), typeElement).name())
                    .title(getClassToolTip(typeElement, true));
        } else if (isPageOrSummaryLink) {
            linkInfo.title(getClassToolTip(typeElement, false));
        }
        Content label = linkInfo.getClassLinkLabel(configuration);
        if (linkInfo.getContext() == HtmlLinkInfo.Kind.SHOW_TYPE_PARAMS_IN_LABEL) {
            // For this kind of link, type parameters are included in the link label
            // (and obviously not added after the link).
            label.add(getTypeParameterLinks(linkInfo));
        }
        Set<ElementFlag> flags;
        Element previewTarget;
        ExecutableElement restrictedTarget;
        boolean showPreview = !linkInfo.isSkipPreview();
        if (isPageOrSummaryLink && showPreview) {
            flags = utils.elementFlags(typeElement);
            previewTarget = typeElement;
            restrictedTarget = null;
        } else if (linkInfo.getContext() == HtmlLinkInfo.Kind.SHOW_PREVIEW
                && linkInfo.getTargetMember() != null && showPreview) {
            // We piggy back on whether to show preview info, for both preview AND
            // restricted methods superscripts. That's because when e.g. we are generating a
            // method summary we do not want either superscript.
            flags = utils.elementFlags(linkInfo.getTargetMember());
            TypeElement enclosing = utils.getEnclosingTypeElement(linkInfo.getTargetMember());
            Set<ElementFlag> enclosingFlags = utils.elementFlags(enclosing);
            if (flags.contains(ElementFlag.PREVIEW) && enclosingFlags.contains(ElementFlag.PREVIEW)) {
                if (enclosing.equals(m_writer.getCurrentTypeElement())) {
                    //skip the PREVIEW tag:
                    flags = EnumSet.copyOf(flags);
                    flags.remove(ElementFlag.PREVIEW);
                }
                previewTarget = enclosing;
            } else {
                previewTarget = linkInfo.getTargetMember();
            }
            if (flags.contains(ElementFlag.RESTRICTED)) {
                restrictedTarget = (ExecutableElement) linkInfo.getTargetMember();
            } else {
                restrictedTarget = null;
            }
        } else {
            flags = EnumSet.noneOf(ElementFlag.class);
            restrictedTarget = null;
            previewTarget = null;
        }

        Content link = new ContentBuilder();
        if (utils.isIncluded(typeElement)) {
            if (configuration.isGeneratedDoc(typeElement) && !utils.isHidden(typeElement)) {
                DocPath fileName = getPath(linkInfo);
                if (linkInfo.linkToSelf() || typeElement != m_writer.getCurrentTypeElement()) {
                        link.add(m_writer.links.createLink(
                                fileName.fragment(linkInfo.getFragment()),
                                label, linkInfo.getStyle(), linkInfo.getTitle()));
                        addSuperscript(link, flags, fileName, null, previewTarget, restrictedTarget);
                        return link;
                }
            }
        } else {
            Content crossLink = m_writer.getCrossClassLink(
                typeElement, linkInfo.getFragment(),
                label, linkInfo.getStyle(), true);
            if (crossLink != null) {
                link.add(crossLink);
                addSuperscript(link, flags, null, typeElement, previewTarget, restrictedTarget);
                return link;
            }
        }
        // Can't link so just write label.
        link.add(label);
        addSuperscript(link, flags, null, null, previewTarget, restrictedTarget);
        return link;
    }

    /**
     * Adds PREVIEW and RESTRICTED superscript labels. Depending on the parameter values,
     * labels will be formatted as local or external links or plain text.
     *
     * @param content the content to add to
     * @param flags the flags
     * @param fileName file name to link to, or null if no local link target
     * @param typeElement external type to link to, or null if no external link
     * @param previewTarget preview link target element
     * @param restrictedTarget restricted link target element
     */
    private void addSuperscript(Content content, Set<ElementFlag> flags, DocPath fileName, TypeElement typeElement,
                                Element previewTarget, ExecutableElement restrictedTarget) {
        Content spacer = Text.EMPTY;
        if (flags.contains(ElementFlag.PREVIEW)) {
            content.add(HtmlTree.SUP(HtmlStyles.previewMark,
                    getSuperscript(fileName, typeElement,
                            m_writer.htmlIds.forPreviewSection(previewTarget),
                            m_writer.contents.previewMark)));
            spacer = Entity.NO_BREAK_SPACE;
        }
        if (flags.contains(ElementFlag.RESTRICTED)) {
            content.add(spacer);
            content.add(HtmlTree.SUP(HtmlStyles.restrictedMark,
                    getSuperscript(fileName, typeElement,
                            m_writer.htmlIds.forRestrictedSection(restrictedTarget),
                            m_writer.contents.restrictedMark)));
        }
    }

    /**
     * Returns PREVIEW or RESTRICTED superscript as either local or external link or as plain text.
     *
     * @param fileName local file name to link to, or null if no local link target
     * @param typeElement external type to link to, or null if no external link
     * @param id the id fragment to link to
     * @param label the label content
     * @return superscript content
     */
    private Content getSuperscript(DocPath fileName, TypeElement typeElement, HtmlId id, Content label) {
        if (fileName != null) {
            return m_writer.links.createLink(fileName.fragment(id.name()), label);
        } else if (typeElement != null) {
            return (m_writer.getCrossClassLink(typeElement, id.name(), label, null, false));
        } else {
            return label;
        }
    }

    /**
     * Returns links to the type parameters.
     *
     * @param linkInfo the information about the link to construct
     * @return the links to the type parameters
     */
    protected Content getTypeParameterLinks(HtmlLinkInfo linkInfo) {
        Content links = newContent();
        List<TypeMirror> vars = new ArrayList<>();
        TypeMirror ctype = linkInfo.getType() != null
                ? utils.getComponentType(linkInfo.getType())
                : null;
        if (linkInfo.getExecutableElement() != null) {
            linkInfo.getExecutableElement().getTypeParameters().forEach(t -> vars.add(t.asType()));
        } else if (linkInfo.getType() != null && utils.isDeclaredType(linkInfo.getType())) {
            vars.addAll(((DeclaredType) linkInfo.getType()).getTypeArguments());
        } else if (ctype != null && utils.isDeclaredType(ctype)) {
            vars.addAll(((DeclaredType) ctype).getTypeArguments());
        } else if (ctype == null && linkInfo.getTypeElement() != null) {
            linkInfo.getTypeElement().getTypeParameters().forEach(t -> vars.add(t.asType()));
        } else {
            // Nothing to document.
            return links;
        }
        if (!vars.isEmpty()) {
            if (linkInfo.addLineBreakOpportunitiesInTypeParameters()) {
                links.add(HtmlTree.WBR());
            }
            links.add("<");
            boolean many = false;
            boolean longTypeParams = vars.stream()
                    .map(t -> getLink(linkInfo.forType(t)))
                    .anyMatch(t -> t.charCount() > ClassWriter.LONG_TYPE_PARAM);
            for (TypeMirror t : vars) {
                if (many) {
                    if (longTypeParams) {
                        links.add(", ");
                    } else {
                        links.add(",").add(HtmlTree.WBR());
                    }
                }
                links.add(getLink(linkInfo.forType(t)));
                many = true;
            }
            links.add(">");
        }
        return links;
    }

    /**
     * Returns links to the type annotations.
     *
     * @param linkInfo the information about the link to construct
     * @return the links to the type annotations
     */
    public Content getTypeAnnotationLinks(HtmlLinkInfo linkInfo) {
        ContentBuilder links = new ContentBuilder();
        List<? extends AnnotationMirror> annotations;
        if (utils.isAnnotated(linkInfo.getType())) {
            annotations = linkInfo.getType().getAnnotationMirrors();
        } else if (utils.isTypeVariable(linkInfo.getType()) && linkInfo.showTypeParameterAnnotations()) {
            Element element = utils.typeUtils.asElement(linkInfo.getType());
            annotations = element.getAnnotationMirrors();
        } else {
            return links;
        }

        if (annotations.isEmpty())
            return links;

        m_writer.getAnnotations(annotations, false)
                .forEach(a -> {
                    links.add(a);
                    links.add(" ");
                });

        return links;
    }

    /*
     * Returns a link info for a type bounds link.
     */
    private HtmlLinkInfo getBoundsLinkInfo(HtmlLinkInfo linkInfo, TypeMirror bound) {
        return linkInfo.forType(bound).skipPreview(false);
    }

    /**
     * Given a class, return the appropriate tool tip.
     *
     * @param typeElement the class to get the tool tip for.
     * @param isTypeParamLink true if link target is a type parameter
     * @return the tool tip for the appropriate class.
     */
    private String getClassToolTip(TypeElement typeElement, boolean isTypeParamLink) {
        Resources resources = m_writer.configuration.getDocResources();
        if (isTypeParamLink) {
            return resources.getText("doclet.Href_Type_Param_Title",
                    utils.getSimpleName(typeElement));
        } else if (utils.isPlainInterface(typeElement)){
            return resources.getText("doclet.Href_Interface_Title",
                m_writer.getLocalizedPackageName(utils.containingPackage(typeElement)));
        } else if (utils.isAnnotationInterface(typeElement)) {
            return resources.getText("doclet.Href_Annotation_Title",
                m_writer.getLocalizedPackageName(utils.containingPackage(typeElement)));
        } else if (utils.isEnum(typeElement)) {
            return resources.getText("doclet.Href_Enum_Title",
                m_writer.getLocalizedPackageName(utils.containingPackage(typeElement)));
        } else {
            return resources.getText("doclet.Href_Class_Title",
                m_writer.getLocalizedPackageName(utils.containingPackage(typeElement)));
        }
    }

    /**
     * Return path to the given file name in the given package. So if the name
     * passed is "Object.html" and the name of the package is "java.lang", and
     * if the relative path is "../.." then returned string will be
     * "../../java/lang/Object.html"
     *
     * @param linkInfo the information about the link.
     */
    private DocPath getPath(HtmlLinkInfo linkInfo) {
        return m_writer.pathToRoot.resolve(docPaths.forClass(linkInfo.getTypeElement()));
    }
}
